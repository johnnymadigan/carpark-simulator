/************************************************
 * @file    simulate-exit.c
 * @author  Johnny Madigan
 * @date    October 2021
 * @brief   Source code for simulate-exit.h
 ***********************************************/
#include <stdio.h>          /* for IO operations */
#include <stdlib.h>         /* for freeing & rand */
#include <string.h>         /* for string operations */
#include <pthread.h>        /* for multi-threading */
#include <time.h>           /* for timespec/nanosleep */

#include "simulate-exit.h"  /* corresponding header */
#include "sleep.h"          /* for boomgate timings */
#include "parking.h"        /* for shared memory types */
#include "queue.h"          /* for queue operations */
#include "sim-common.h"     /* for flag & rand lock */

void *simulate_exit(void *args) {

    /* -----------------------------------------------
     *    DECONSTRUCT ARGS & LOCATE SHARED EXIT
     * -------------------------------------------- */
    args_t *a = (args_t *)args;
    queue_t *q = a->queue;
    exit_t *ex = (exit_t*)((char *)shm + a->addr);

    /* -----------------------------------------------
     *          GATE STARTS OFF CLOSED
     * -------------------------------------------- */
    pthread_mutex_lock(&ex->gate.lock);
    ex->gate.status = 'C';
    pthread_mutex_unlock(&ex->gate.lock);

    /* -----------------------------------------------
     *       LOOP WHILE SIMULATION HASN'T ENDED
     * -------------------------------------------- */
    while (!end_simulation) {

        /* -----------------------------------------------
         *               LOCK ENTRANCE's QUEUE
         * -----------------------------------------------
         * Wait until there is at least 1 car waiting in the queue,
         * using IF rather than WHILE so when simulation has ended,
         * Main can wake up these threads, and instead of waiting
         * again, threads can skip the rest of the loop and return
         */
        pthread_mutex_lock(&ex_queues_lock);
        while (q->head == NULL && !end_simulation) {
            pthread_cond_wait(&ex_queues_cond, &ex_queues_lock);
        }
        car_t *c = pop_queue(q);
        pthread_mutex_unlock(&ex_queues_lock);

        /* -----------------------------------------------
         *         CHECK IF GATE IS LOWERING
         *        (ONLY STAYS OPENED FOR 20ms)
         * -----------------------------------------------
         * Once opened, the gate will stay opened for 20ms 
         * then the Manager will lower the gate, and we will
         * close it here
         */
        pthread_mutex_lock(&ex->gate.lock);
        if (ex->gate.status == 'L') {
            sleep_for_millis(10);
            ex->gate.status = 'C';
        }

        /* -----------------------------------------------
         *           OPEN GATE IF THERE IS A FIRE   
         * -----------------------------------------------
         * In the event of a fire, the gate will be raised
         * by the Fire Alarm System, and we will open it here
         */
        if (ex->gate.status == 'R') {
            sleep_for_millis(10);
            ex->gate.status = 'O';
        }
        pthread_mutex_unlock(&ex->gate.lock);
        pthread_cond_broadcast(&ex->gate.condition);

        if (c != NULL && !end_simulation) {
            /* -----------------------------------------------
             *        IMMEDIATELY TRIGGER LPR SENSOR
             * THEN UNLOCK & BROADCAST LPR SO MAN CHECKS IT
             * -----------------------------------------------
             * specification does not say to wait 2ms like entrance (so immediately trigger) */
            pthread_mutex_lock(&ex->sensor.lock);
            strcpy(ex->sensor.plate, c->plate);
            pthread_mutex_unlock(&ex->sensor.lock);
        
            /* 8 millisecond pause before we broadcast to the Manager
            that the LPR is ready, this is so that we can allow the 
            DISPLAY STATUS thread to read & display status of LPR */

            /* we DON'T use our custom sleep_for_millis function because the client can slow down
            time using the "SLOW MOTION" variable but we want this time to be constistent */
            int millis = 8; 
            struct timespec remaining, requested = {(millis / 1000), ((millis % 1000) * 1000000)};
            nanosleep(&requested, &remaining);

            pthread_cond_broadcast(&ex->sensor.condition);

            /* -----------------------------------------------
             *        IF GATE IS CLOSED? WAIT FOR IT START RAISING
             *        THEN FINISH RAISING AND OPEN (10ms)
             *        THEN BROADCAST TO "MANAGE-GATE" THREADS
             *        SO GATE STAYS OPEN FOR 20ms BEFORE LOWERING
             * -------------------------------------------- */
            pthread_mutex_lock(&ex->gate.lock);
            while (ex->gate.status == 'C' && !end_simulation) pthread_cond_wait(&ex->gate.condition, &ex->gate.lock);
            if (ex->gate.status == 'R') {
                sleep_for_millis(10);
                ex->gate.status = 'O';
            }
            pthread_mutex_unlock(&ex->gate.lock);
            pthread_cond_broadcast(&ex->gate.condition);

            free(c); /* car leaves Sim */
        }
    }
    free(args);
    return NULL;
}