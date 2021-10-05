/************************************************
 * @file    simulate-entrance.c
 * @author  Johnny Madigan
 * @date    September 2021
 * @brief   Source code for simulate-entrance.h
 ***********************************************/
#include <stdio.h>              /* for IO operations */
#include <stdlib.h>             /* for freeing & rand */
#include <string.h>             /* for string operations */
#include <pthread.h>            /* for multi-threading */

#include "simulate-entrance.h"  /* corresponding header */
#include "sleep.h"              /* for boomgate timing */
#include "parking.h"            /* for shared memory types */
#include "queue.h"              /* for queue operations */
#include "sim-common.h"         /* for flag & rand lock */
#include "car-lifecycle.h"      /* for sending authorised cars off */

void *simulate_entrance(void *args) {

    /* -----------------------------------------------
     *    DECONSTRUCT ARGS & LOCATE SHARED ENTRANCE
     * -------------------------------------------- */
    args_t *a = (args_t *)args;
    queue_t *q = a->queue;
    entrance_t *en = (entrance_t*)((char *)shm + a->addr);

    /* -----------------------------------------------
     *          GATE STARTS OF CLOSED
     *          LPR STARTS OF EMPTY
     *          SIGN STARTS OF BLANK
     * -------------------------------------------- */
    pthread_mutex_lock(&en->gate.lock);
    en->gate.status = 'C';
    pthread_mutex_unlock(&en->gate.lock);

    pthread_mutex_lock(&en->sensor.lock);
    strcpy(en->sensor.plate, "");
    pthread_mutex_unlock(&en->sensor.lock);

    pthread_mutex_lock(&en->sign.lock);
    en->sign.display = 0;
    pthread_mutex_unlock(&en->sign.lock);

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
        pthread_mutex_lock(&en_queues_lock);
        if (q->head == NULL) pthread_cond_wait(&en_queues_cond, &en_queues_lock);
        car_t *c = pop_queue(q);
        pthread_mutex_unlock(&en_queues_lock);

        /* -----------------------------------------------
         *         CHECK IF GATE IS LOWERING
         * ----------------------------------------------*/
        pthread_mutex_lock(&en->gate.lock);
        if (en->gate.status == 'L') {
            sleep_for_millis(10);
            en->gate.status = 'C';
        }
        pthread_mutex_unlock(&en->gate.lock);

        if (c != NULL) {
            /* -----------------------------------------------
             *      2ms BEFORE TRIGGERING LPR SENSOR
             * -------------------------------------------- */
            sleep_for_millis(2);
            pthread_mutex_lock(&en->sensor.lock);
            strcpy(en->sensor.plate, c->plate);
            pthread_mutex_unlock(&en->sensor.lock);
            pthread_cond_signal(&en->sensor.condition);

            /* -----------------------------------------------
             *      LOCK THE SIGN THEN
             *      WAIT FOR THE MANAGER TO VALIDATE PLATE
             *      AND UPDATE THE SIGN
             * -------------------------------------------- */
            pthread_mutex_lock(&en->sign.lock);
            while (en->sign.display == 0) pthread_cond_wait(&en->sign.condition, &en->sign.lock);

            /* -----------------------------------------------
             *      IF NOT AUTHORISED OR CAR PARK IS FULL
             * -------------------------------------------- */
            if (en->sign.display == 'X' || en->sign.display == 'F') {
                free(c); /* car leaves Sim */

            /* -----------------------------------------------
             *         IF AUTHORISED & ASSIGNED A LEVEL
             * -------------------------------------------- */
            } else {
                c->floor = (int)en->sign.display - '0'; /* assign to floor */

                /* -----------------------------------------------
                 *        IF GATE IS CLOSED? WAIT FOR IT START RAISING
                 *        THEN FINISH RAISING AND OPEN (10ms)
                 *        THEN BROADCAST TO "MANAGE-GATE" THREADS
                 *        SO GATE STAYS OPEN FOR 20ms BEFORE LOWERING
                 * -------------------------------------------- */
                pthread_mutex_lock(&en->gate.lock);
                while (en->gate.status == 'C') pthread_cond_wait(&en->gate.condition, &en->gate.lock);
                if (en->gate.status == 'R') {
                    sleep_for_millis(10);
                    en->gate.status = 'O';
                }
                pthread_mutex_unlock(&en->gate.lock);
                pthread_cond_broadcast(&en->gate.condition);

                /* -----------------------------------------------
                 * SEND CARS OFF IN THEIR OWN "CAR-LIFECYCLE" THREAD
                 * -----------------------------------------------
                 * As cars move independently once inside.
                 */
                pthread_t new_car_thread;
                pthread_create(&new_car_thread, NULL, car_lifecycle, (void *)c);
            }

            /* -----------------------------------------------
             *                  UNLOCK SIGN
             * -------------------------------------------- */
            en->sign.display = 0; /* reset sign */
            pthread_mutex_unlock(&en->sign.lock);
        }
    }
    free(args);
    return NULL;
}