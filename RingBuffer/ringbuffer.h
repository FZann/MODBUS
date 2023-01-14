/*
 * ringbuffer.h
 *
 *  Created on: 7 mar 2022
 *      Author: fabizani
 */

#ifndef RING_BUFFER_RINGBUFFER_H_
#define RING_BUFFER_RINGBUFFER_H_

#include <stdint.h>

/**************************************************************************************************
 * 									  OPTIONS FROM DEFINE
 **************************************************************************************************/

/**************************************************************************************************
 * 										SAFETY CHECKS
 **************************************************************************************************/

/**************************************************************************************************
 * 										TYPE DECLARATION
 **************************************************************************************************/
// Dichiarazione dell'ADT - Il puntatore è costante, non si può riassegnare dopo la prima volta
typedef struct sRing* hRingBuffer;

/**************************************************************************************************
 * 									  VARIABLE DECLARATION
 **************************************************************************************************/

/**************************************************************************************************
 * 										METODI DELL'ADT
 **************************************************************************************************/
hRingBuffer RingNew(uint16_t size);


// adds a byte to the ring buffer
void RingAdd(hRingBuffer buff, uint8_t u8Val);

// gets all the available bytes into buffer and return the number of bytes read
uint8_t RingGetAllBytes(hRingBuffer buff, uint8_t *buffer);

// gets uNumber of bytes from ring buffer, returns the actual number of bytes read
uint8_t RingGetNBytes(hRingBuffer buff, uint8_t *buffer, uint8_t uNumber);

// return the number of available bytes
uint8_t RingCountBytes(hRingBuffer buff);

// flushes the ring buffer
void RingClear(hRingBuffer buff);

#endif /* RING_BUFFER_RINGBUFFER_H_ */
