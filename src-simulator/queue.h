/************************************************
 * @file    queue.h
 * @author  Johnny Madigan
 * @date    September 2021
 * @brief   API for initialising, modifying, and clearing
 *          a queue. Queues being a line of cars waiting.
 ***********************************************/
#pragma once

#include <pthread.h>    /* for mutexes and conditions */
#include <stdbool.h>    /* for bool type */

typedef struct car_t {
    char plate[7];  /* 6 chars +1 for string null terminator */
    int floor;      /* keep note of assigned floor */
    long duration;  /* milliseconds */
} car_t;

typedef struct node_t {
    car_t *car;
    struct node_t *next;
} node_t;

typedef struct queue_t {
    node_t *head;   /* front of the line */
    node_t *tail;   /* back of the line */
} queue_t;

/**
 * @brief Initialises a queue before use.
 * 
 * @param q - q to initialise 
 */
void init_queue(queue_t *q);

/**
 * @brief Pushes a car to the end of the queue.
 * 
 * @param q - queue to push to
 * @param c - car to push
 * @return true - if push successful
 * @return false - if push failed
 */
bool push_queue(queue_t *q, car_t *c);

/**
 * @brief Pops head of the queue. The first node will be
 * deconstructed then freed, returning only the car itself
 * 
 * @param q - queue to pop head
 * @return car_t* - car at the front of the queue
 */
car_t *pop_queue(queue_t *q);

/**
 * @brief Prints all cars' license plates in a queue
 * 
 * @param q - queue to check
 */
void print_queue(queue_t *q);

/**
 * @brief Empties a queue by freeing all items, where the head
 * and tail become NULL again.
 * 
 * @param q - queue to empty
 */
void empty_queue(queue_t *q);