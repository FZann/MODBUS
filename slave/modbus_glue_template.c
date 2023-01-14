/*
 * modbus_glue.c
 *
 *  Created on: 10 mar 2022
 *      Author: fabizani
 *
 * Questo file racchiude il codice "collante" tra la libreria MODBUS e l'applicazione.
 * Gli indirizzi dei vari dati sono dichiarati nel file "modbus_map.h".
 *
 * Le funzioni sono richiamate dalla libreria MODBUS, in modalità Slave (Server), durante
 * l'elaborazione delle frame ricevute dal Master (Client).
 *
 * Qui si implementano le funzioni dichiarate nell'header "modbus_slave.h".
 *
 * 							NON INSERIRE INDIRIZZI CON "MAGIC NUMBERS"!!!!!
 *
 * Vietato dai principi della buona programmazione!
 *
 */

#include <modbus_map_1.0.0.h>
#include <modbus_slave.h>

/* Inserire eventuali include provenienti dall'applicazione esterna */

hMODBUS MODBUS;


/**************************************************************************************************
 * 								FUNZIONI LETTURA/SCRITTURA COILS
 **************************************************************************************************/
sMODBUS_ReadResult MODBUS_readCoils(uint16_t address) {
	sMODBUS_ReadResult result = { .data = 0, .error = Exception_NoException };

	switch (address) {

    /* Inserire i case provenienti dalla modbus_map.h */

	default:
		result.error = Exception_IllegalAddr;
		break;
	}

	return result;
}

eMODBUS_Excpt MODBUS_writeCoils(uint16_t address, uint16_t data) {
	switch (address) {

    /* Inserire i case provenienti dalla modbus_map.h */

	default:
		result.error = Exception_IllegalAddr;
		return result;

	}

	return Exception_NoException;
}

/**************************************************************************************************
 * 									FUNZIONI LETTURA DISCRETES
 **************************************************************************************************/
sMODBUS_ReadResult MODBUS_readDiscretes(uint16_t address) {
	sMODBUS_ReadResult result = { .data = 0, .error = Exception_NoException };

	switch (address) {

    /* Inserire i case provenienti dalla modbus_map.h */

	default:
		result.error = Exception_IllegalAddr;
		return result;
	}

	return result;
}

/**************************************************************************************************
 * 							FUNZIONI LETTURA/SCRITTURA HOLDINGS REGISTERS
 **************************************************************************************************/
sMODBUS_ReadResult MODBUS_readHoldings(uint16_t address) {
	sMODBUS_ReadResult result = { .data = 0, .error = Exception_NoException };

	switch (address) {

    /* Inserire i case provenienti dalla modbus_map.h */

	default:
		result.error = Exception_IllegalAddr;
		break;
	}

	return result;
}

/**************************************************************************************************
 * 							FUNZIONI LETTURA/SCRITTURA INPUT REGISTERS
 **************************************************************************************************/
sMODBUS_ReadResult MODBUS_readInputs(uint16_t address) {
	sMODBUS_ReadResult result = { .data = 0, .error = Exception_NoException };

    switch (address) {

    /* Inserire i case provenienti dalla modbus_map.h */

	default:
		result.error = Exception_IllegalAddr;
		break;
	}

	return result;
}

eMODBUS_Excpt MODBUS_writeInputs(uint16_t address, uint16_t data) {
	switch (address) {

    /* Inserire i case provenienti dalla modbus_map.h */

	}

	return Exception_NoException;
}

/**************************************************************************************************
 *							FUNZIONE DI CALLBACK SCRITTURA TERMINATA
 **************************************************************************************************/
void MODBUS_WriteCallback() {

    /* Inserire eventuale codice di call-back da lanciare al termine della scrittura dei dati MODBUS */

}
