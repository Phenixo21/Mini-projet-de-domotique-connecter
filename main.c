//============================================================================================
// Appelle des différentes librairie ainsi que des fichier.h
//============================================================================================
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "capteur.h"
#include "bouton.h"
#include <fcntl.h>
#include <gpiod.h>
#include "reseau.h"
#include <syslog.h>
#include "logs.h"

volatile int ordre_prio=0; // valeur de priorité pour le controle de la led par le serveur

//=============================================================================================
//Initialisation du thread capteur.
//L 'objectif de ce thread est de lire périodiquement la tempéature et l'humidité via le bus I2C.
//Si la température dépasse 25°c,un signal est envoyer dans la pipe pour allumer la led sinon un
//siganl contaire est envoyer pour éteindre la led.
//=============================================================================================
void* thread_capteur(void* arg) {
    int file_pipe=*(int*)arg;
    int file;
    char sig;

    fcntl(file_pipe, F_SETFL, O_NONBLOCK);//Mode non bloquant pour la lecture du pipe
    float t = 0.0, h = 0.0; // Initialisation propre

    while(1) {
		file=capteur();//ouverture du bus I2C et connexion au capteur
		if (file <0){
			printf("Erreur bus I2C inaccessible, tentative dans 5 seconde...\\n");
			log_error("Bus I2C inaccessible");
		}else{
			lecture_capteur(file,&t,&h);//lecture des valeurs de température et d'humidité

			//contrôle de la led si la température est superieurs à 25°C et s'il n'y a pas d'ordre de priorité 
			if(t>25.0){
				char alerte='L';
				write(file_pipe,&alerte,1);
				log_info(" Température supérieurs à 25°c, led allumée");
				}else{
					char alerte ='l';
					write(file_pipe,&alerte,1);
					log_info("Température normal, led eteinte");
				}

			close(file);
		}
        printf("DEBUG : Boucle active...\n"); 
		fflush(stdout);

        //lecture_capteur(file, &t, &h);
		//
		//lecture du pipe pour détecter un siganl reste du thread bouton
        if(read(file_pipe, &sig, 1) > 0){
            if (sig == 'R'){
                printf("Reset bouton!\n");
                fflush(stdout);
            }
        }
        sleep(5);
    }

    close(file);
    return NULL;
}

//===============================================================================================================
//Initialisation du thread bouton
//surveille la GPIO du bouton en attente d'un front descendant.
//Lis le pipe pour allumer ou éteindre la led selon le siganl reçu
//en cas d'appuis sur le bouton: reste de l'application
//===============================================================================================================
void* thread_bouton(void* arg) {
    int pipe_fd = *(int*)arg;
    char sig = 'R';

    // Initialisation GPIO17 via la libgpio
    struct gpiod_line_request *request = bouton_init();
    if (!request) {
        printf("[BOUTON] Impossible d'initialiser GPIO17, thread arrêté.\n");
        fflush(stdout);
        return NULL;
    }

    struct gpiod_edge_event_buffer *buffer = gpiod_edge_event_buffer_new(1);

	fcntl(pipe_fd,F_SETFL,O_NONBLOCK);// mode non bloquant pour la lecture du pipe
	char msg;
    printf("[BOUTON] Thread prêt sur GPIO17\n");
    fflush(stdout);

    while (1) {
		//lecture du pipe qui conduit à un controle de la led depuis le thread capteur ou reseau
		if(read(pipe_fd, &msg,1)>0){
			if(msg=='L'){
				led_set(request,1); // allumer la led
				fflush(stdout);
			}else{
				led_set(request,0); // eteindre la led
				}
		}

		//Attente d'un événment sur la GPIO avec un timeout de 100ms
        int ret = gpiod_line_request_wait_edge_events(request, 100000000);

        if (ret < 0) {
            perror("[ERREUR] wait_edge_events a échoué");
            sleep(1);
            continue;
        }

        if (ret > 0) {
			//appuie bouton détécter donc reset de l'aplication
            gpiod_line_request_read_edge_events(request, buffer, 1);

            printf("\n[RESET] Appui détecté ! Effacement des logs...\n");
	    log_info("Reset application");
	    ordre_prio=0;
	    led_set(request,0);

		//vide le fichier logs
            FILE *f_log = fopen("data.log", "w");
            if (f_log) {
                fprintf(f_log, "--- LOG RESET ---\n");
                fclose(f_log);
                printf("[OK] Fichier data.log réinitialisé.\n");
            } else {
                perror("[ERREUR] Impossible de vider les logs");
            }

			//signale le reset au thread capteur
            write(pipe_fd, &sig, 1);
            fflush(stdout);
            usleep(500000); // Anti-rebond
        }
    }

	//on libére les buffers
    gpiod_edge_event_buffer_free(buffer);
    gpiod_line_request_release(request);
    return NULL;
}

/*void log_error(const char *erreur_message){
	syslog(LOG_ERR,"Erreur : %s", erreur_message);
}*/


//===============================================================================================================
//Initialisation des logs, création des pipe de communication entre les thread puis ordre de lancement des thread capteur, bouton, reseau (server).
//attent la fin des threads avant de fermer les logs
//==============================================================================================================
int main(void) {
    pthread_t t_capteur;
    pthread_t t_bouton; 
	pthread_t t_reseau;
    int pipe_file[2];

	log_init();
	log_info("Initialisation des logs de Log_domo");

	//openlog("Log_domo", LOG_PID | LOG_CONS, LOG_USER); // creation des log syslog
	//syslog(LOG_INFO,"initialisation des logs de Log_domo");

    if (pipe(pipe_file) == -1){
        perror("erreur pipe");
		log_error("impossible d'ouvrir un pipe");
        return -1;
    }

    //  Démarrer le BOUTON EN PREMIER pour stabiliser le GPIO
    if (pthread_create(&t_bouton, NULL, thread_bouton, (void*)&pipe_file[0]) != 0){
        perror("Erreur création thread bouton");
		log_error("impôssible de créer le thread bouton");
        return -1;
    }

    // Attendre que libgpiod ait fini de configurer le matériel
    sleep(1);

    // Démarrer le CAPTEUR I2C ensuite
    if (pthread_create(&t_capteur, NULL, thread_capteur, (void*)&pipe_file[1]) != 0) {
        perror("Erreur création thread capteur");
		log_error("impossible de créer le thread capteur");
        return -1;
    }

	sleep(1);

	//lancement du thread server UDP
	if(pthread_create(&t_reseau, NULL, thread_reseau,(void*)&pipe_file[1])!=0){
			perror("Erreur creation thread reseau");
			log_error(" impossible de créer le trhead serveur");
			return -1;
	}


    pthread_join(t_capteur, NULL);
    pthread_join(t_bouton, NULL);
	pthread_join(t_reseau, NULL);

	logs_close();
    return 0;
}
