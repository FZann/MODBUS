/*
 * modbus.h
 *
 *  Created on: 3 mar 2022
 *      Author: fabizani
 *
 *  Modulo per la gestione del protocollo MODBUS RTU.
 *  La libreria utilizza gli ADT (Abstract Data Types) per l'implementazione di più oggetti MODBUS,
 *  potendo così pilotare differenti porte seriali con differenti settaggi di MODBUS.
 *
 *  STILE NOTAZIONE:
 *  - variabili: viene inserito un prefisso di uno o più caratteri minuscoli che indica il tipo dati
 *    originario della variabile stessa.
 *    ES: 	sMODBUS 	->	struct MODBUS
 *    		eFuncCode 	->	enum FuncCode
 */

#ifndef MODBUS_MODBUS_H_
#define MODBUS_MODBUS_H_

#include <stdint.h>
#include "RingBuffer/ringbuffer.h"
#include "usart.h"

/**************************************************************************************************
 * 									  OPTIONS FROM DEFINE
 **************************************************************************************************/

/**************************************************************************************************
 * 										SAFETY CHECKS
 **************************************************************************************************/

/**************************************************************************************************
 * 										TYPE DECLARATION
 **************************************************************************************************/

/*
 * Dichiarazione delle enumerazioni
 */

/// L'enum racchiude tutti i vari codici-funzione (comandi) implementati in questa libreria.
typedef enum {
	FC_ReadCoilStatus = 1,
	FC_ReadDiscreteInputs = 2,
	FC_ReadHoldingRegisters = 3,
	FC_ReadInputRegisters = 4,
	FC_WriteSingleCoil = 5,
	FC_WriteSingleRegister = 6,
	FC_WriteMultipleCoils = 15,
	FC_WriteMultipleRegisters = 16
} eMODBUS_FuncCode;

typedef enum {
	MODBUS_Mode_Master, MODBUS_Mode_Slave,
} eMODBUS_Mode;

typedef enum {
	// MODBUS compliant
	Exception_IllegalFunc = 1,
	Exception_IllegalAddr = 2,
	Exception_InvalidDataValue = 3,
	Exception_DevFailure = 4,
	Exception_ACK = 5,
	Exception_Busy = 6,

	// Personali - uso interno della libreria
	Exception_NoException = 0,
	Exception_InvalidFrame = 100,
} eMODBUS_Excpt;

/*
 *  Dichiarazione dell'ADT e altre strutture di supporto
 */
/// ADT dell'oggetto MODBUS. In questo modo non facciamo fuoriuscire i campi interni della struct.
typedef struct sMODBUS MODBUS_t;

/**
 * Struttura per il passaggio di comandi alla stack MODBUS in modalità MASTER. <br>
 * Servono per comandare all'oggetto l'invio di dati sul bus; possono essere accodati, permettendo
 * l'esecuzione asincrona di più comandi.
 */
typedef struct {
	eMODBUS_FuncCode functionCode;
	uint8_t slaveID;
	uint16_t regAddress;
	uint16_t length;
} sMODBUS_Commmand;

/**
 * Struttura per il passaggio dei dati dall'applicazione verso la libreria. <br>
 * Serve da interfaccia tra le due parti, consentendo di non toccare il codice di libreria.
 */
typedef struct {
	uint16_t data;			///< Dati richiesti. Letti dalla memoria del dispositivo
	eMODBUS_Excpt error;	///< Eventuale errore nella lettura. Potrebbe essere un indirizzo non
							/// corretto o una lettura non implementata dal dispositivo.
} sMODBUS_ReadResult;

/*
 *  Dichiarazione dei puntatori a funzione
 */

/// Interfaccia per la lettura dei dati dalla memoria del dispositivo
typedef sMODBUS_ReadResult (*MODBUS_LocalRead)(const uint16_t);

/// Interfaccia per la scrittura dei dati nella memoria del dispositivo
typedef eMODBUS_Excpt (*MODBUS_LocalWrite)(const uint16_t, const uint16_t);

/// Callback eseguita al termine di un evento; verrà chiamata solo se impostata
typedef void (*MODBUS_Event)(void);

/// Callback eseguita nel momento in cui si verifica un'eccezione nella stack MODBUS
typedef void(*MODBUS_Exception)(eMODBUS_Excpt);

/// Callback eseguita dopo aver ricevuto una frame di dati da remoto; chiamata in modalità Master
typedef void (*MODBUS_RemoteData)(const uint8_t, const uint16_t, const uint16_t);

/**
 *  Callback eseguita al momento della spedizione dei dati
 *  @param MODBUS_t* Puntatore all'oggetto MODBUS che ha originato la richiesta di trasmissione
 *  @param uint8_t*  Puntatore al buffer dei dati da spedire
 *  @param uint8_t   Lunghezza del buffer da spedire
 */
typedef void (*MODBUS_DataTx)(const MODBUS_t*, const uint8_t *, const uint8_t);

/**************************************************************************************************
 * 									  VARIABLE DECLARATION
 **************************************************************************************************/

/**************************************************************************************************
 * 										METODI DELL'ADT
 **************************************************************************************************/
MODBUS_t* MODBUS_NewHandle(UART_HandleTypeDef *port);
void MODBUS_DeleteHandle(MODBUS_t *handle);

void MODBUS_ExecuteTask(MODBUS_t *handle);


/*
 * SETTERS
 */
void MODBUS_SetAddress(MODBUS_t *handle, uint8_t *address);
void MODBUS_SetMode(MODBUS_t *handle, eMODBUS_Mode mode);
void MODBUS_SetWriteCompleteCallback(MODBUS_t *handle, MODBUS_Event writeCmplt);
void MODBUS_SetRemoteCmptCallback(MODBUS_t *handle, MODBUS_Event remoteCmplt);
void MODBUS_SetRemoteErrorCallback(MODBUS_t *handle, MODBUS_Exception remoteError);
void MODBUS_SetRxTimeoutCallback(MODBUS_t *handle, MODBUS_Event rxTimeout);
void MODBUS_SetHwDataTx(MODBUS_t *handle, MODBUS_DataTx hwDataTx);


/*
 * GETTERS
 */
uint8_t MODBUS_GetMyAddress(const MODBUS_t *handle);
eMODBUS_Mode MODBUS_GetMode(const MODBUS_t *handle);
UART_HandleTypeDef *MODBUS_GetUART(const MODBUS_t *handle);


/*
 * FUNZIONE DI GESTIONE DEI VARI REGISTRI
 */
void MODBUS_Coils_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn);
void MODBUS_Coils_SetWritingFn(MODBUS_t *handle, MODBUS_LocalWrite writeFn);
void MODBUS_Coils_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn);

void MODBUS_Discretes_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn);
void MODBUS_Discretes_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn);

void MODBUS_Holdings_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn);
void MODBUS_Holdings_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn);

void MODBUS_Inputs_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn);
void MODBUS_Inputs_SetWritingFn(MODBUS_t *handle, MODBUS_LocalWrite writeFn);
void MODBUS_Inputs_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn);


/*
 * HARDWARE e RX
 */
void MODBUS_SaveByte(MODBUS_t *handle, const uint8_t u8Data);
void MODBUS_SetRxComplete(MODBUS_t *handle);
uint8_t MODBUS_GetRxComplete(MODBUS_t *handle);
void MODBUS_MasterTickRxTimer(MODBUS_t *handle);

/*
 * MASTER TX - accodamento dei comandi
 */
void MODBUS_QueueCommand(MODBUS_t *handle, sMODBUS_Commmand *cmd);


#endif /* MODBUS_MODBUS_H_ */
