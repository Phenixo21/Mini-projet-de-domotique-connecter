# Nom de l'exécutable final
TARGET = min-projet

# Compilateur et options
CC = gcc
CFLAGS = -Wall -Wextra -I./capteur -I./bouton -I./reseau -I./logs
LDFLAGS = -lpthread -lgpiod

# Liste des fichiers sources (on va les chercher là où ils sont)
SRCS = main.c capteur/capteur.c bouton/3bouton.c reseau/reseau.c logs/logs.c
# Génération de la liste des fichiers objets (.o)
OBJS = $(SRCS:.c=.o)

# Règle par défaut
all: $(TARGET)

# Compilation de l'exécutable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Règle pour transformer chaque .c en .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyage des fichiers temporaires
clean:
	rm -f $(OBJS) $(TARGET)

# Pour éviter les conflits si un fichier s'appelle 'clean'
.PHONY: all clean
