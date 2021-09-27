/*******************************************************
 * @file    manager.c
 * @author  Johnny Madigan
 * @date    September 2021
 * @brief   Main file for the manager software. Automates
 *          aspects of running a carpark. All of the manager's
 *          header files link back here.
 ******************************************************/
#include <stdio.h>      /* for print, scan... */
#include <stdlib.h>     /* for malloc, free... */
#include <string.h>     /* for string stuff... */
#include <stdbool.h>    /* for bool stuff... */
#include <pthread.h>    /* for threads */
#include <ctype.h>      /* for isalpha, isdigit... */
#include <fcntl.h>      /* for file modes like O_RDWR */
#include <sys/mman.h>   /* for mapping shared like MAP_SHARED */

/* header APIs + read config file */
#include "parking-types.h"
#include "plates-hash-table.h"
#include "../config.h"

#define SHARED_MEM_NAME "PARKING"
#define SHARED_MEM_SIZE 2920 /* bytes */
#define TABLE_SIZE 100 /* buckets for authorised license plates */

_Atomic int current_cap = 0; // initially none
htab_t *plates_ht;

//pthread_mutex_t current_cap_lock; /* as the currently occupied count is global */
pthread_mutex_t plates_ht_lock; /* as the hash table is global */

/* entrance thread args */
typedef struct en_args_t {
    int number;
    void *shared_memory;
} en_args_t;

/* function prototypes */
bool validate_plate(char *plate);
void *manage_entrance(void *args);

int main(int argc, char **argv) {
    /* READ AUTHORISED LICENSE PLATES LINE-BY-LINE
    INTO HASH TABLE, VALIDATING EACH PLATE */
    plates_ht = new_hashtable(TABLE_SIZE);

    puts("Opening plates.txt...");
    FILE *fp = fopen("plates.txt", "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }
    puts("Reading plates.txt...");

    char line[1000]; /* ensure whole line is read */

    while (fgets(line, sizeof(line), fp) != NULL) {

        /* scan line for first occurance of the newline 
        and replace with a null terminator */
        line[strcspn(line, "\n")] = 0;
        
        /* if string is valid, add to database */
        if (validate_plate(line)) {
            hashtable_add(plates_ht, line);
        }
    }
    fclose(fp);

    /* for debugging...
    print_hashtable(plates_ht);
    */


    /* LOCATE THE SHARED MEMORY OBJECT */
    int shm_fd;
    char *shm;

    if ((shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0)) < 0) {
        perror("Opening shared memory");
        exit(1);
    }

    /* attach the shared memory segment to this data space */
    if ((shm = mmap(0, SHARED_MEM_SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0)) == (char *)-1) {
        perror("Mapping shared memory");
        exit(1);
    }
    
    
    /* MANAGE ENTRANCE THREADS */
    pthread_t en_threads[ENTRANCES];
    
    for (int i = 0; i < ENTRANCES; i++) {
        // args will be freed within relevant thread
        en_args_t *args = malloc(sizeof(en_args_t) * 1);
        args->number = i;
        args->shared_memory = shm;

        pthread_create(&en_threads[i], NULL, manage_entrance, (void *)args);
    }


    /* join ALL threads before cleanup */
    for (int i = 0; i < ENTRANCES; i++) {
        pthread_join(en_threads[i], NULL);
    }

    /* destroy license plates' hash table */
    hashtable_destroy(plates_ht);
    return EXIT_SUCCESS;
}



void *manage_entrance(void *args) {

    /* deconstruct args */
    en_args_t *a = (en_args_t *)args;
    int floor = a->number;
    void *shm = a->shared_memory;

    /* locate associated shared memory data for this entrance */
    entrance_t *en = (entrance_t*)(shm + (sizeof(entrance_t) * floor));

    for (;;) {
        //puts("looping...");

        pthread_mutex_lock(&en->sensor.lock);
        /* wait until LPR hardware reads in a number plate */
        while (strcmp(en->sensor.plate, "") == 0) {
            //puts("waiting for simulation to read plate");

            pthread_cond_wait(&en->sensor.condition, &en->sensor.lock);
        }

        /* validate plate, locking the hash table as it is global */
        pthread_mutex_lock(&plates_ht_lock);
        bool authorised = hashtable_find(plates_ht, en->sensor.plate);

        pthread_mutex_unlock(&plates_ht_lock);

        /* after validating, clear the info sign */
        strcpy(en->sensor.plate, "");
        pthread_mutex_unlock(&en->sensor.lock);

        /* update infosign to direct the car to either enter or leave */
        pthread_mutex_lock(&en->sign.lock);
        //pthread_mutex_lock(&current_cap_lock);

        if (!authorised) {
            en->sign.display = 'X';

        } else if (current_cap >= (CAPACITY * LEVELS)) {
            en->sign.display = 'F';

        } else if (authorised) {
            current_cap++; /* reserve space as car is authorised */
            en->sign.display = '1'; /* goto floor 1 for now */
            printf("%d\n", current_cap);
            pthread_mutex_lock(&en->gate.lock);
            /* if closed? raise */
            if (en->gate.status == 'C') {
                //puts("about to raise gate");
                en->gate.status = 'R';
            }
            pthread_mutex_unlock(&en->gate.lock);
            pthread_cond_signal(&en->gate.condition);
        }
        /* unlock current capacity, sign, and signal simulator */
        //pthread_mutex_unlock(&current_cap_lock);
        pthread_mutex_unlock(&en->sign.lock);
        pthread_cond_signal(&en->sign.condition);
        
        /* close gate if left open */
        pthread_mutex_lock(&en->gate.lock);
        //printf("current gate status is %c\n", en->gate.status);
        /* while gate status is still raising, wait - there WILL be a signal */
        while (en->gate.status == 'R') {
            //prevent race condition
            pthread_cond_wait(&en->gate.condition, &en->gate.lock);
        }
        /* if open? lower */
        if (en->gate.status == 'O') {
            //puts("about to lower gate");
            en->gate.status = 'L';
        }
        pthread_mutex_unlock(&en->gate.lock);
        pthread_cond_signal(&en->gate.condition);

        //puts("completed cycle");
    }

    return NULL;
}


/**
 * Validates license plate strings via their format.
 * 
 * @param plate to validate
 * @return true if valid, false if not
 */
bool validate_plate(char *p) {
    /* check if plate is correct length */
    if (strlen(p) != 6) {
        return false;
    }

    /* slice string in half - first 3, last 3 */
    char first[4];
    char last[4];
    strncpy(first, p, 3);
    strncpy(last, p + 3, 3);
    first[3] = '\0';
    last[3] = '\0';

    /* for debugging...
    printf("FIRST3\t%s\n", first);
    printf("LAST3\t%s\n", last);
    */

    /* check if plate is correct format: 111AAA */
    for (int i = 0; i < 3; i++) {
        if (!(isdigit(first[i]) && isalpha(last[i])))  {
            return false;
        }
    }

    /* plate is valid */
    return true;
}


/*
gcc -o ../MANAGER manager.c plates-hash-table.c -Wall -lpthread -lrt
*/