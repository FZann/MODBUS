/*
 * ringbuffer.c
 *
 *  Created on: 7 mar 2022
 *      Author: fabizani
 */

#include <stdlib.h>
#include "ringbuffer.h"

/**************************************************************************************************
 * 										DEFINEs & CONSTs
 **************************************************************************************************/
#define true 1
#define false 0

/**************************************************************************************************
 * 										TYPE DEFINITION
 **************************************************************************************************/

struct sRing {
	uint8_t *u8Buffer;

	uint16_t u16BufferSize;
	uint8_t u8start;
	uint8_t u8end;
	uint8_t u8available;
	uint8_t u8overflow;
};

/**************************************************************************************************
 *									DICHIARAZIONI PRIVATE
 **************************************************************************************************/

/**************************************************************************************************
 * 										FUNZIONI PRIVATE
 **************************************************************************************************/

/**************************************************************************************************
 * 										METODI DELL'ADT
 **************************************************************************************************/
hRingBuffer RingNew(uint16_t size) {
	hRingBuffer buff = calloc(1, sizeof(struct sRing));
	buff->u8Buffer = calloc(size, sizeof(uint8_t));
	buff->u16BufferSize = size;

	return buff;
}

void RingAdd(hRingBuffer buff, uint8_t u8Val) {

	buff->u8Buffer[buff->u8end] = u8Val;
	buff->u8end = (buff->u8end + 1) % buff->u16BufferSize;
	if (buff->u8available == buff->u16BufferSize) {
		buff->u8overflow = true;
		buff->u8start = (buff->u8start + 1) % buff->u16BufferSize;
	} else {
		buff->u8overflow = false;
		buff->u8available++;
	}

}

__attribute__((always_inline))
inline uint8_t RingGetAllBytes(hRingBuffer buff, uint8_t *buffer) {
	return RingGetNBytes(buff, buffer, buff->u8available);
}

uint8_t RingGetNBytes(hRingBuffer buff, uint8_t *buffer, uint8_t uNumber) {
	uint8_t uCounter;
	if (buff->u8available == 0 || uNumber == 0)
		return 0;
	if (uNumber > buff->u16BufferSize)
		return 0;

	for (uCounter = 0; uCounter < uNumber && uCounter < buff->u8available; uCounter++) {
		buffer[uCounter] = buff->u8Buffer[buff->u8start];
		buff->u8start = (buff->u8start + 1) % buff->u16BufferSize;
	}
	buff->u8available = buff->u8available - uCounter;
	buff->u8overflow = false;
	RingClear(buff);

	return uCounter;
}

uint8_t RingCountBytes(hRingBuffer buff) {
	return buff->u8available;
}

void RingClear(hRingBuffer buff) {
	buff->u8start = 0;
	buff->u8end = 0;
	buff->u8available = 0;
	buff->u8overflow = false;
}
