/************************************************
 * @file    manager.c
 * @author  Johnny Madigan
 * @date    September 2021
 * @brief   Main file for the Manager software.
 *          Automates aspects of running a carpark.
 *          Threads branch out from here.
 ***********************************************/
#include <stdio.h>      /* for print, scan... */
#include <stdlib.h>     /* for malloc, free... */
#include <string.h>     /* for string stuff... */
#include <stdbool.h>    /* for bool stuff... */
#include <pthread.h>    /* for threads */
#include <ctype.h>      /* for isalpha, isdigit... */
#include <fcntl.h>      /* for file modes like O_RDWR */
#include <sys/mman.h>   /* for mapping shared like MAP_SHARED */

#include <time.h>
#include <unistd.h>

/* header APIs + read config file */
#include "plates-hash-table.h"
#include "manage-entrance.h"
#include "manage-exit.h"
#include "display-status.h"
#include "man-common.h"
#include "../config.h"

#define SHARED_MEM_NAME "PARKING"
#define SHARED_MEM_SIZE 2920    /* bytes */
#define TABLE_SIZE 100          /* buckets for hash tables */

/* init externs from man-common.h */
void *shm;
int *curr_capacity;
pthread_mutex_t curr_capacity_lock;
pthread_cond_t curr_capacity_cond;
_Atomic int revenue = 0; /* initially $0 */
_Atomic int total_cars_entered = 0;
htab_t *auth_ht;
htab_t *bill_ht;
pthread_mutex_t auth_ht_lock;
pthread_mutex_t bill_ht_lock;
pthread_cond_t auth_ht_cond;
pthread_cond_t bill_ht_cond;

/* function prototypes */
void read_file(char *name, htab_t *table);
bool validate_plate(char *plate);

/**
 * @brief   Entry point for the MANAGER software.
 *          Opens shared memory (exit if not found).
 *          Creates # tables before branches off to other 
 *          threads to manage entrances, levels and exits.
 * 
 * @param   argc - argument count, a standard param
 * @param   argv - arguments, a standard param
 * @return  int - indicating program's success or failure 
 */
int main(int argc, char **argv) {

    /* calloc array to keep track of each lvl's current capacity,
    all capacities are initially 0 meaning no spaces are taken */
    curr_capacity = calloc(LEVELS, sizeof(int));

    /* create new # tables to store car info for authorising/billing */
    auth_ht = new_hashtable(TABLE_SIZE);
    bill_ht = new_hashtable(TABLE_SIZE);

    /* ----------READ AUTHORISED LICENSE PLATES FILE---------- */
    puts("Reading plates.txt");
    read_file("plates.txt", auth_ht);

    /* ----------LOCATE THE SHARED MEMORY OBJECT---------- */
    int shm_fd;

    if ((shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0)) < 0) {
        perror("Opening shared memory");
        exit(1);
    }

    /* attach the shared memory to this data space */
    if ((shm = mmap(0, SHARED_MEM_SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0)) == (char *)-1) {
        perror("Mapping shared memory");
        exit(1);
    }

    /* ----------START ENTRANCE, EXIT, & STATUS THREADS---------- */
    pthread_t en_threads[ENTRANCES];
    pthread_t ex_threads[EXITS];
    pthread_t status_thread;

    for (int i = 0; i < ENTRANCES; i++) {
        args_t *a = malloc(sizeof(args_t) * 1);
        a->id = i;
        pthread_create(&en_threads[i], NULL, manage_entrance, (void *)a);
    }

    for (int i = 0; i < EXITS; i++) {
        args_t *a = malloc(sizeof(args_t) * 1);
        a->id = i;
        pthread_create(&ex_threads[i], NULL, manage_exit, (void *)a);
    }

    pthread_create(&status_thread, NULL, display, NULL);

    /* ----------CLEAN-UP---------- */
    /* ----------JOIN ALL THREADS BEFORE EXIT---------- */
    for (int i = 0; i < ENTRANCES; i++) pthread_join(en_threads[i], NULL);
    for (int i = 0; i < EXITS; i++) pthread_join(ex_threads[i], NULL);
    pthread_join(status_thread, NULL);

    /* unmap shared mem, destroy # tables, and free capacities array */
    munmap((void *)shm, SHARED_MEM_SIZE);
    hashtable_destroy(auth_ht);
    hashtable_destroy(bill_ht);
    free(curr_capacity);
    return EXIT_SUCCESS;
}

/**
 * @brief Opens (or creates file if it does not exist) for reading.
 * Each line will be validated as a license plate. If valid, the plate
 * will be added to the # table of authorised cars.
 * 
 * @param name - name of the file to open/create
 * @param table - # table to add valid plates to
 */
void read_file(char *name, htab_t *table) {
    FILE *fp = fopen(name, "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }
    
    char line[1000]; /* buffer to ensure whole line is read */

    while (fgets(line, sizeof(line), fp) != NULL) {

        /* scan line for first occurance of the newline 
        and replace with a null terminator */
        line[strcspn(line, "\n")] = 0;
        
        /* if string is valid, add to authorised # table */
        if (validate_plate(line)) hashtable_add(table, line, 0);
    }

    /* for debugging...
    print_hashtable(auth_ht);
    */

    fclose(fp);
}

/**
 * @brief Validates license plate strings via their format (111AAA).
 * 
 * @param p - plate to validate
 * @return true - if valid
 * @return false - if illegal
 */
bool validate_plate(char *p) {
    /* check if plate is correct length */
    if (strlen(p) != 6) return false;
    
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
        if (!(isdigit(first[i]) && isalpha(last[i]))) return false;
    }

    return true;
}