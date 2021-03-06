/************************************************
 * @file    simulate-temp.h
 * @author  Johnny Madigan
 * @date    October 2021
 * @brief   API for simulating temperature
 ***********************************************/
#pragma once

/**
 * @brief Updates temperature per floor every 1..5 millis.
 * Algorithm used keeps the temperature within the global
 * MIN and MAX values (configurable in config.h). As temperature
 * changes, it can only go up or down by 1 degree.
 * 
 * @param args - collection of items
 * @return void* - NULL upon completion
 */
void *simulate_temp(void *args);