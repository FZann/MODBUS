/**
 * @file modbus.c
 *
 *  Created on: 3 mar 2022
 *      Author: fabizani
 *
 * Il file racchiude il sorgente della libreria MODBUS RTU Paso.
 * Utilizza la tecnica degli Abstract Data Types per nascondere tutti i dettagli implementativi
 * della libreria. Per accedere ai vari dati sono forniti i getters relativi.
 *
 *
 *
 *
 */

#include <Core/modbus_core.h>
#include <stdlib.h>
#include <string.h>

#include "cQueue/cQueue.h"

/**************************************************************************************************
 * 										DEFINEs & CONSTs
 *************************************************************************************************/

#define INLINE inline __attribute__((always_inline))
#define FIRE_EVENT(event)\
if(event != 0) \
	event();

#define FIRE_EVENT_1PAR(event, param)\
if(event != 0) \
	event(param);


/* TIMEOUT_BITS dipende dalle specifiche del MODBUS: possiamo essere Slave o Master.
 * Quando viene effettuata una ricezione in modalità Slave, abbiamo 1.5 volte il tempo di word per
 * ricere la successiva parola (11 bits/word * 1.5).
 * In modalità Master abbiamo 3.5 volte il tempo di word per ricevere la risposta dallo Slave.
 */
#define SLAVE_BITS_TIMEOUT				17
#define MASTER_BITS_TIMEOUT				38
#define MODBUS_FRAME_MAX_SIZE			260
#define MASTER_HEADER_BYTES				6
#define SLAVE_HEADER_BYTES				3
#define MASTER_FRAME_LENGTH				8
#define SLAVE_FRAME_LENGTH				6

#define QUEUED_COMMANDS					16
#define RX_TIMEOUT_ms					250

/**************************************************************************************************
 * 										TYPE DEFINITION
 *************************************************************************************************/

/// Struttura per "spacchettare" una word (Cortex-M = u32) del processore in diversi formati. <br>
/// N.B.: ARM è un'architettura Little-Endian, quindi salva per primo il byte più basso!
typedef union {
	uint32_t u32;		///< Word intera del processore
	uint16_t u16[2];	///< "Spacchettata" della word in due u16
	uint8_t u8[4];		///< "Spacchettata" della word in quattro u8
} bytesFields;

/// Enum per la gestione degli stati di ricezione del Master
typedef enum {
	Rx_NOP,
	Rx_Waiting,
	Rx_OK,
	Rx_ErrorFrame,
	Rx_Timeout,
} eRxState;

/**
 * Pacchetto ricevuto/inviato dal MASTER. Racchiude tutti i vari campi necessari per il corretto
 * funzionamento del protocollo MODBUS. Non è stato limitato lo scopo d'accesso ii campi interni,
 * quindi la presente libreria può accedere direttamente ai dati.
 * La sMaster_Frame, tuttavia, rimane locale al file 'modbus_core.c', perché questo tipo dati
 * è necessario solo in questo contesto.
 */
typedef struct {
	uint16_t u16Length;	///< Lunghezza totale del pacchetto. Gestita dai metodi del MODBUS
	union {
		uint8_t raw[MODBUS_FRAME_MAX_SIZE];	///< Dati grezzi per semplificare la spedizione UART
		struct {
			// 6 Byte sono standard per tutte le frame
			// Address e ReadLength sono da 16 bit, ma purtroppo sono spediti in Big Endian
			// e letti in Little Endian dalla macchina ARM. Per questo sono stati spezzati
			uint8_t u8DevID;			///< ID dello slave target
			uint8_t u8FuncCode;			///< Function code relativo al pacchetto
			uint8_t u8AddressHigh;		///< High-byte del campo "Indirizzo"
			uint8_t u8AddressLow;		///< Low-byte del campo "Indirizzo"
			uint8_t u8Length_High;		///< High-byte del campo "Length"
			uint8_t u8Length_Low;		///< Low-byte del campo "Length"

			/// Quello che segue dipende dal tipo di frame
			union {
				/// Parte valida se la frame è di lettura o scrittura singola
				struct {
					uint8_t u8CRChigh;	///< High-byte del CRC
					uint8_t u8CRClow;	///< Low-byte del CRC
				};

				/// Parte valida se la frame è di scrittura multipla
				struct {
					uint8_t u8ByteCount;		///< Byte totali dei dati spediti
					uint8_t u8PayloadStart;		///< Indica il punto dove partono i byte dati
				};
			};
		};
	};
} sMaster_Frame;

typedef struct {
	uint16_t u16Length;	///< Lunghezza totale del pacchetto. Gestita dai metodi del MODBUS
	union {
		uint8_t raw[MODBUS_FRAME_MAX_SIZE];	///< Dati grezzi per semplificare la ricezione UART
		struct {
			uint8_t u8DevID;		///< ID dello slave target
			uint8_t u8FuncCode;		///< Function code relativo al pacchetto
			uint8_t u8ByteCount;	///< Byte totali dei dati ricevuti
		};
	};
} sSlave_Frame;

/// Definizione dell'interfaccia per le funzioni di aggiunta dati alla frame Slave
typedef void (*AppendToFrame)(sSlave_Frame*, bytesFields);

/// Definizione dell'interfaccia per le funzioni di decodifica del payload dalla frame Slave
typedef uint16_t (*readPayload)(sSlave_Frame*, uint16_t);

/** Struttura che serve da interfaccia per tutti metodi interni di lettura/scrittura dei vari tipi
 * di dato del MODBUS. sRegister memorizza i puntatori a funzione che vengono poi utilizzati per
 * modificare il comportamento in base al tipo di dato richiesto (Strategy design pattern).
 */
typedef struct {
	MODBUS_LocalRead reading;	///< Funzione utente di lettura dei dati
	MODBUS_LocalWrite writing;	///< Funzione utente di scrittura dei dati
	MODBUS_RemoteData remote;	///< Evento di ricezioni dati remoti (Master Mode)

	AppendToFrame appendData;	///< Funzione di libreria che genera la frame slave di risposta
	readPayload readPayload;	///< Funzione di libreria che decodifica i dati dalla frame slave
} sRegister;

/**
 * Function pointer che gestisce la stack MODBUS.i sono callbacks che gestiscono gli eventi generati
 * dalla libreria. Questo permette di eseguire costantemente in back-ground questa funzione,
 * come fosse effettivamente un task di un sistema operativo.
 * Il puntatore permettela modifica del comportamento dell'oggetto MODBUS in base alla modalità
 * selezionata: master o slave.
 * Lo slave risponde ai messaggi ricevuti e non fa altro;
 * il master si incarica di inviare la richiesta e attendere la risposta dello slave interrogato.
 */
typedef void (*ExecuteTask)(MODBUS_t*);

/// Definizione dell'Abstract Data Type del MODBUS
/**
 * Struttura che racchiude tutti i dati per gestire una stack MODBUS. <br>
 * La tipologia di implementazione ADT permette di instanziare diversi oggetti ed eseguire
 * i metodi in modalità rientrante.
 */
struct sMODBUS {
	UART_HandleTypeDef *pxCom;		///< Handle di una porta UART della HAL di STMCube
	uint8_t *u8myAddress;	///< L'indirizzo del protocollo MODBUS
	eMODBUS_Mode uxMode;	///< Modalità MODBUS: Master o Slave

	Queue_t commands;			///< Coda per salvare i comandi ricevuti
	sMODBUS_Commmand lastCmd;	///< Ultimo comando estratto dalla coda in modalità Master

	uint8_t u8RxComplete;	///< Flag di ricezione completata
	uint16_t u16RxTimeout;	///< Timeout di ricezione: se scade, torna ad accodare comandi
	hRingBuffer pxRxBuff;	///< Puntatore al Ring Buffer che salva i dati ricevuti

	sRegister coils;		///< Interfaccia per le funzioni dei registri Coils
	sRegister discretes;	///< Interfaccia per le funzioni dei registri Discretes
	sRegister inputs;		///< Interfaccia per le funzioni dei registri Inputs
	sRegister holdings;		///< Interfaccia per le funzioni dei registri Holdings

	ExecuteTask task;		///< Funzione pricipale della stack MODBUS; simula un task di un SO
	MODBUS_Event writeCmpltCallback;		///< Evento di termine scrittura dati
	MODBUS_Event remoteRxOKCallback;		///< Evento lanciato al termine di una lettura remota
	MODBUS_Exception remoteRxErrorCallback;	///< Ricevuta frame errata dallo slave
	MODBUS_Event rxTimeout;					///< Evento lanciato allo scadere del timeout ricezione
	MODBUS_DataTx hwDataTx;					///< Trasmissione dei dati all'hardware
};

/**************************************************************************************************
 *									VARIABILI PRIVATE
 *************************************************************************************************/

/**************************************************************************************************
 *									DICHIARAZIONI PRIVATE
 *************************************************************************************************/
eMODBUS_Excpt ReadMasterFrame(MODBUS_t *const handle, sMaster_Frame *mFrame);
eMODBUS_Excpt ReadSlaveFrame(MODBUS_t *const handle, sSlave_Frame *sFrame);
sSlave_Frame setupExceptionFrame(const sMaster_Frame *mFrame, eMODBUS_Excpt excpt);

// Funzioni per l'elaborazione della risposta
sSlave_Frame ReadValues(MODBUS_t *handle, const sMaster_Frame *mFrame);
sSlave_Frame WriteSingle(MODBUS_t *handle, const sMaster_Frame *mFrame);
sSlave_Frame WriteMultipleCoils(MODBUS_t *handle, const sMaster_Frame *mFrame);
sSlave_Frame WriteMultipleRegisters(MODBUS_t *handle, const sMaster_Frame *mFrame);

void FrameSlave_AppendCoil(sSlave_Frame *sFrame, bytesFields data);
void FrameSlave_AppendRegister(sSlave_Frame *sFrame, bytesFields data);

uint16_t FrameSlave_ReadCoils(sSlave_Frame *sFrame, uint16_t address);
uint16_t FrameSlave_ReadRegisters(sSlave_Frame *sFrame, uint16_t address);

void FrameSlave_AppendCRC(sSlave_Frame *sFrame);
void FrameMaster_AppendCRC(sMaster_Frame *mFrame);

uint16_t calcCRC(const uint8_t *Buffer, uint8_t u8length);

// Implementazione dummy per le funzioni di read/write esterne. Usando queste funzioni vuote
// possiamo eseguire lo stesso l'applicazione senza avere dei segfault e ritornare delle eccezioni.
sMODBUS_ReadResult dummyReadingFunction(const uint16_t address);
eMODBUS_Excpt dummyWritingFunction(const uint16_t address, const uint16_t data);
void dummyTxData(const MODBUS_t *handle, const uint8_t *data, const uint8_t len);

// TASK DI ELABORAZIONE DELLA STACK MODBUS
void MODBUS_SlaveTask(MODBUS_t *handle);

// Il task Master è suddiviso in più stati, in quanto deve effettuare azioni differenti durante
// l'esecuzione del task. Sfruttiamo il function pointer già esistente per creare le varie
// funzioni di stato. Sarà la funzione stessa a cambiare il puntatore verso il nuovo stato.
void MODBUS_MasterTask_WaitAndSendCommand(MODBUS_t *handle);
void MODBUS_MasterTask_WaitRx(MODBUS_t *handle);
void MODBUS_MasterTask_ElaborateRx(MODBUS_t *handle);

/**************************************************************************************************
 * 										FUNZIONI PRIVATE
 *************************************************************************************************/

eMODBUS_Excpt ReadMasterFrame(MODBUS_t *handle, sMaster_Frame *mFrame) {
	mFrame->u16Length = RingGetAllBytes(handle->pxRxBuff, &mFrame->raw[0]);

	// Dobbiamo avere almeno 8 byte per una corretta frame MODBUS,
	// più la corrispondenza dell'indirizzo
	if (mFrame->u16Length < MASTER_FRAME_LENGTH || mFrame->u8DevID != *handle->u8myAddress)
		return Exception_InvalidFrame;

	uint8_t len = 0;
	switch (mFrame->u8FuncCode) {
	case FC_ReadCoilStatus:
	case FC_ReadDiscreteInputs:
	case FC_ReadHoldingRegisters:
	case FC_ReadInputRegisters:
	case FC_WriteSingleCoil:
	case FC_WriteSingleRegister:
		len = MASTER_HEADER_BYTES;
		break;

	case FC_WriteMultipleCoils:
	case FC_WriteMultipleRegisters:
		// La lunghezza è pari alla Header, più il byte del ByteCount,
		// più il numero di byte indicati dal ByteCount stesso
		len = MASTER_HEADER_BYTES + mFrame->u8ByteCount + 1;
		break;

	default:
		return Exception_IllegalFunc;
		break;
	}
	uint16_t crc = 0;
	uint16_t crcRx = 0;
	crc = calcCRC(&mFrame->raw[0], len);

	// Leggo la parte bassa e alta del CRC ricevuto
	crcRx = mFrame->raw[len + 0] << 8; 	// Alta
	crcRx |= mFrame->raw[len + 1];			// Bassa

	// Verifichiamo che il CRC sia corretto
	if (crc != crcRx)
		return Exception_InvalidFrame;

	// Tutto a posto
	return Exception_NoException;
}

eMODBUS_Excpt ReadSlaveFrame(MODBUS_t *const handle, sSlave_Frame *sFrame) {
	sFrame->u16Length = RingGetAllBytes(handle->pxRxBuff, &sFrame->raw[0]);

	if (sFrame->u16Length < SLAVE_FRAME_LENGTH)
		return Exception_InvalidFrame;

	uint8_t len = 0;
	switch (sFrame->u8FuncCode) {
	// La lunghezza è varibile in base a quanti byte sono stati letti
	case FC_ReadCoilStatus:
	case FC_ReadDiscreteInputs:
	case FC_ReadHoldingRegisters:
	case FC_ReadInputRegisters:
		len = SLAVE_HEADER_BYTES + sFrame->u8ByteCount;
		break;

		// Le frame sono tutte lunghe 6 byte + 2b CRC, come una Master Frame
	case FC_WriteSingleCoil:
	case FC_WriteSingleRegister:
	case FC_WriteMultipleCoils:
	case FC_WriteMultipleRegisters:
		len = MASTER_HEADER_BYTES;	// Gli ultimi 2 byte sono il CRC, da escludere
		break;

	default:
		return Exception_IllegalFunc;
		break;
	}

	uint16_t crc = 0;
	uint16_t crcRx = 0;
	crc = calcCRC(&sFrame->raw[0], len);

	// Leggo la parte bassa e alta del CRC ricevuto
	crcRx = sFrame->raw[len + 0] << 8; 	// Alta
	crcRx |= sFrame->raw[len + 1];			// Bassa

	// Verifichiamo che il CRC sia corretto
	if (crc != crcRx)
		return Exception_InvalidFrame;

	// Tutto a posto
	return Exception_NoException;
}

/**
 * @brief Questa funzione imposta una sSlave_Frame partedo dalla mFrame.
 */
sSlave_Frame setupExceptionFrame(const sMaster_Frame *mFrame, eMODBUS_Excpt excpt) {
	sSlave_Frame sFrame;

	// Imposta il primo bit del FC a 1 per segnalare l'eccezione
	sFrame.u8DevID = mFrame->u8DevID;
	sFrame.u8FuncCode = 0x80 | mFrame->u8FuncCode;

	// Nel pacchetto, l'eccezione ha la stessa posizione del byte di ByteCount
	sFrame.u8ByteCount = excpt;

	uint16_t crc = calcCRC(&sFrame.raw[0], 3);
	sFrame.raw[3] = crc >> 8;
	sFrame.raw[4] = crc;
	sFrame.u16Length = 5;

	return sFrame;
}

sSlave_Frame ReadValues(MODBUS_t *handle, const sMaster_Frame *mFrame) {
	uint16_t readLength = (mFrame->u8Length_High << 8) + mFrame->u8Length_Low;
	uint16_t AddressOffset = (mFrame->u8AddressHigh << 8) + mFrame->u8AddressLow;
	uint16_t u16EndAdd = AddressOffset + readLength;

	// Con questo switch selezioniamo il tipo di informazioni che si vogliono elaborare:
	// grazie ai puntatori a funzione tutto il resto del codice rimane uguale per tutte
	// le tipologie di lettura. Effettivamente il codice è comune; se si fossero implementate
	// 4 funzioni diverse si avrebbe avuto molto codice doppiato.
	sRegister SelectedReg;
	switch (mFrame->u8FuncCode) {
	case FC_ReadCoilStatus:
		SelectedReg = handle->coils;
		break;
	case FC_ReadDiscreteInputs:
		SelectedReg = handle->discretes;
		break;
	case FC_ReadHoldingRegisters:
		SelectedReg = handle->holdings;
		break;
	case FC_ReadInputRegisters:
		SelectedReg = handle->inputs;
		break;
	}

	sSlave_Frame sFrame;
	sFrame.u8DevID = mFrame->u8DevID;
	sFrame.u8FuncCode = mFrame->u8FuncCode;
	sFrame.u8ByteCount = 0;
	sFrame.u16Length = 3;

	for (uint16_t u16Add = AddressOffset, reps = 0; u16Add < u16EndAdd; u16Add++, reps++) {
		sMODBUS_ReadResult result;
		bytesFields data;

		// La funzione reading deve essere implementata lato utente: è quella che fa da collante
		// tra questa libreria MODBUS (che non deve essere modificata) e i dati dell'applicazione,
		// scritti e salvati in formati non compatibili con le specifiche MODBUS.
		// In questo modo possiamo raccimolare dati da qualsiasi cella di memoria, anche non
		// contigue, e farle sembrare contigue per il protocollo MODBUS.
		result = SelectedReg.reading(u16Add);

		if (result.error != Exception_NoException)
			return setupExceptionFrame(mFrame, result.error);

		// Passiamo i dati e le ripetizioni del ciclo; queste ultime servono per coils/discretes
		// per formattare correttamente i bytes della frame
		data.u16[0] = result.data;
		data.u16[1] = reps;

		SelectedReg.appendData(&sFrame, data);
	}

	FrameSlave_AppendCRC(&sFrame);

	return sFrame;
}

/**
 * @relates MODBUS_SlaveTask
 * #brief Funzione che gestisce i Function-Codes MODBUS che eseguono una scrittura di una singola
 * cella di memoria.
 */
sSlave_Frame WriteSingle(MODBUS_t *handle, const sMaster_Frame *mFrame) {
	// In questo caso i dati sono al posto della ReadLength
	uint16_t u16WriteAdd = (mFrame->u8AddressHigh << 8) + mFrame->u8AddressLow;
	uint16_t u16Data = (mFrame->u8Length_High << 8) + mFrame->u8Length_Low;

	// Con questo switch selezioniamo il tipo di informazioni che si vogliono elaborare:
	// grazie ai puntatori a funzione tutto il resto del codice rimane uguale per le varie
	// tipologie di scrittura.
	sRegister SelectedReg;
	switch (mFrame->u8FuncCode) {
	case FC_WriteSingleCoil:
		SelectedReg = handle->coils;
		if (u16Data == 0xFF00)
			u16Data = 1;
		else if (u16Data == 0x0000)
			u16Data = 0;
		else
			return setupExceptionFrame(mFrame, Exception_InvalidDataValue);
		break;

	case FC_WriteSingleRegister:
		SelectedReg = handle->inputs;
		break;
	}

	eMODBUS_Excpt error = SelectedReg.writing(u16WriteAdd, u16Data);

	if (error != Exception_NoException)
		return setupExceptionFrame(mFrame, error);

	// Tutto ok. Setup della risposta, che è esattamente uguale alla richiesta.
	// Quindi ne facciamo una copia brutale brutale, usando il buffer RAW
	sSlave_Frame sFrame;
	sFrame.u16Length = 6;
	memcpy(&sFrame.raw[0], &mFrame->raw[0], mFrame->u16Length);
	FrameSlave_AppendCRC(&sFrame);
	return sFrame;
}

/* Il problema nell'unificare le due funzioni di WriteMultiple è che varia drasticamente
 * l'esecuzione del ciclo for. Per ovviare a questo problema si dovrebbero creare due
 * 'if()' per identificare le coils e i registri... di fatto implementando due funzioni
 * in una sola. Non ha senso. Anche coi puntatori a funzione dovrebbero essere passati come minimo
 * 4 parametri, quindi creando delle funzioni "troppo grosse".
 * Non ho trovato implementazione migliore per tenere il codice DRY.
 */

sSlave_Frame WriteMultipleCoils(MODBUS_t *handle, const sMaster_Frame *mFrame) {
	uint16_t writeLength = (mFrame->u8Length_High << 8) + mFrame->u8Length_Low;
	uint16_t AddressOffset = (mFrame->u8AddressHigh << 8) + mFrame->u8AddressLow;
	uint16_t u16EndAdd = AddressOffset + writeLength;

	// ReadIndex lo facciamo partire 1 byte prima rispetto alla posizione reale del payload,
	// perché lo incrementiamo immediatamente alla prima ripetizione del ciclo for.
	// Effettivamente i dati partono dal byte 7 della frame (cioè mFrame->raw[7]).
	uint16_t readIndex = 6;
	for (uint16_t u16Add = AddressOffset, reps = 0; u16Add < u16EndAdd; u16Add++, reps++) {
		// Abbiamo terminato di leggere questo byte, passiamo al successivo.
		if (reps % 8 == 0)
			readIndex++;

		// Estrae il singolo bit dal byte della frame.
		bytesFields data;
		data.u16[0] = (mFrame->raw[readIndex] >> (reps % 8)) & 0x01;
		handle->coils.writing(u16Add, data.u16[0]);
	}

	// Tutto ok. Setup della risposta, che è uguale ai primi 6 byte della richiesta.
	// Quindi ne facciamo una copia brutale brutale, usando il buffer RAW
	sSlave_Frame sFrame;
	sFrame.u16Length = 6;
	memcpy(&sFrame.raw[0], &mFrame->raw[0], mFrame->u16Length);
	FrameSlave_AppendCRC(&sFrame);
	return sFrame;
}

sSlave_Frame WriteMultipleRegisters(MODBUS_t *handle, const sMaster_Frame *mFrame) {
	uint16_t writeLength = (mFrame->u8Length_High << 8) + mFrame->u8Length_Low;
	uint16_t AddressOffset = (mFrame->u8AddressHigh << 8) + mFrame->u8AddressLow;
	uint16_t u16EndAdd = AddressOffset + writeLength;

	// ReadIndex identifica la posizione dalla quale partono i dati della frame master
	// (cioè mFrame->raw[7]). Lo incrementiamo di 2 ad ogni ripetizione per muoverci sui dati u16
	uint16_t readIndex = 7;
	for (uint16_t u16Add = AddressOffset; u16Add < u16EndAdd; u16Add++, readIndex += 2) {

		bytesFields data;

		// Inversione dovuta al Big-Endian MODBUS / Little-Endian ARM
		data.u8[0] = mFrame->raw[readIndex + 1];
		data.u8[1] = mFrame->raw[readIndex + 0];

		eMODBUS_Excpt error = handle->inputs.writing(u16Add, data.u16[0]);

		// Check dell'errore; con questa implementazione, però, i dati precedenti all'eccezione
		// sono ugualmente scritti nella memoria. Non so se sia corretto oppure no.
		if (error != Exception_NoException)
			return setupExceptionFrame(mFrame, error);
	}

	// Tutto ok. Setup della risposta, che è uguale ai primi 6 byte della richiesta.
	// Quindi ne facciamo una copia brutale brutale, usando il buffer RAW
	sSlave_Frame sFrame;
	sFrame.u16Length = 6;
	memcpy(&sFrame.raw[0], &mFrame->raw[0], mFrame->u16Length);
	FrameSlave_AppendCRC(&sFrame);
	return sFrame;
}

/**
 * @relates sRegister
 * @brief Funzione per l'accodamento dei dati nella frame Slave di risposta ad una richiesta
 * ricevuta precedentemente da un MASTER.
 */
void FrameSlave_AppendCoil(sSlave_Frame *sFrame, bytesFields data) {
	// Quando il rimanente è zero aumentiamo di 1 byte la frame.
	// Lo facciamo subito per allocare lo spazio al byte dati delle coils.
	// Siccome il buffer è statico, può avere dentro ancora i dati precedenti.
	// Prima di aggiungere il nuovo byte faccio un azzeramento, così elimino i dati vecchi
	if (data.u16[1] % 8 == 0) {
		sFrame->raw[sFrame->u16Length] = 0;
		sFrame->u16Length += 1;
		sFrame->u8ByteCount += 1;
	}

	// In data.u16[0] abbiamo i dati, mentre in data.u16[1] abbiamo lo shift del bit di coil
	// Viene calcolato il rimanente % 8 sullo shift per riempire un singolo byte
	// Usiamo Length - 1 perché gli array sono indicizzati a 0
	sFrame->raw[sFrame->u16Length - 1] |= (data.u16[0] & 0x01) << (data.u16[1] % 8);
}

/**
 * @relates sRegister
 * Funzione per l'accodamento dei dati nella frame Slave di risposta ad una richiesta
 * ricevuta precedentemente da un MASTER.
 */
void FrameSlave_AppendRegister(sSlave_Frame *sFrame, bytesFields data) {
	// Allocazione in Big-Endian; ARM gestisce in Little-Endian. Per questo invertiamo i byte
	// I dati sono nei primi 16 bit
	sFrame->raw[sFrame->u16Length + 0] = data.u8[1];
	sFrame->raw[sFrame->u16Length + 1] = data.u8[0];

	sFrame->u16Length += 2;
	sFrame->u8ByteCount += 2;
}

uint16_t FrameSlave_ReadCoils(sSlave_Frame *sFrame, uint16_t address) {
	uint8_t bitNum = address % 8;
	uint8_t byteNum = address / 8 + SLAVE_HEADER_BYTES;

	return ((sFrame->raw[byteNum] & (1 << bitNum)) != 0) ? 1 : 0;

}

uint16_t FrameSlave_ReadRegisters(sSlave_Frame *sFrame, uint16_t address) {
	// Protocollo Big-Endian: prima leggo il byte alto e dopo quello basso
	uint8_t hiByte = address * 2 + SLAVE_HEADER_BYTES;
	uint8_t loByte = hiByte + 1;

	return (sFrame->raw[hiByte] << 8) + sFrame->raw[loByte];
}

/**
 * Calcolo del CRC della sSlave_Frame e ne accoda il risultato per la spedizione. <br>
 * Chiamare questa funzione solo dopo aver terminato di accodare tutti i dati nella frame.
 */
void FrameSlave_AppendCRC(sSlave_Frame *sFrame) {
	uint16_t crc = calcCRC(&sFrame->raw[0], sFrame->u16Length);
	sFrame->raw[sFrame->u16Length + 0] = crc >> 8;
	sFrame->raw[sFrame->u16Length + 1] = crc;

	sFrame->u16Length += 2;
}

void FrameMaster_AppendCRC(sMaster_Frame *mFrame) {
	uint16_t crc = calcCRC(&mFrame->raw[0], mFrame->u16Length);
	mFrame->raw[mFrame->u16Length + 0] = crc >> 8;
	mFrame->raw[mFrame->u16Length + 1] = crc;

	mFrame->u16Length += 2;
}

sMaster_Frame FrameMaster_FromCommand(sMODBUS_Commmand *cmd) {
	sMaster_Frame mFrame;
	mFrame.u8DevID = cmd->slaveID;
	mFrame.u8FuncCode = cmd->functionCode;
	mFrame.u8AddressHigh = cmd->regAddress >> 8;
	mFrame.u8AddressLow = cmd->regAddress & 0xff;
	mFrame.u8Length_High = cmd->length >> 8;
	mFrame.u8Length_Low = cmd->length & 0xff;
	mFrame.u16Length = MASTER_HEADER_BYTES;
	FrameMaster_AppendCRC(&mFrame);

	return mFrame;
}

/**
 * Funzione di appoggio che esegue i calcoli matematici del CRC del MODBUS.
 */
uint16_t calcCRC(const uint8_t *Buffer, uint8_t u8length) {
	uint16_t temp, temp2, flag;
	temp = 0xFFFF;
	for (uint8_t i = 0; i < u8length; i++) {
		temp = temp ^ Buffer[i];
		for (uint8_t j = 1; j <= 8; j++) {
			flag = temp & 0x0001;
			temp >>= 1;
			if (flag)
				temp ^= 0xA001;
		}
	}
	// Reverse byte order.
	temp2 = temp >> 8;
	temp = (temp << 8) | temp2;
	temp &= 0xFFFF;
	// the returned value is already swapped
	// crcLo byte is first & crcHi byte is last
	return temp;
}

sMODBUS_ReadResult dummyReadingFunction(const uint16_t address) {
	// Ritorna un'eccezione per indicare un problema nell'implementazione delle funzioni
	sMODBUS_ReadResult result = { .data = 0, .error = Exception_IllegalFunc };
	return result;
}

eMODBUS_Excpt dummyWritingFunction(const uint16_t address, const uint16_t data) {
	// Ritorna un'eccezione per indicare un problema nell'implementazione delle funzioni
	return Exception_IllegalFunc;
}

void dummyTxData(const MODBUS_t *handle, const uint8_t *data, const uint8_t len) {

}

/**************************************************************************************************
 * 										METODI DELL'ADT
 *************************************************************************************************/
/** @brief Crea un nuovo oggetto MODBUS; racchiude tutte le impostazioni del protocollo.
 * N.B.: Allocato dinamicamente! Non inserire in loop o interrupt questa funzione!
 */
MODBUS_t* MODBUS_NewHandle(UART_HandleTypeDef *port) {
	MODBUS_t *handle = calloc(1, sizeof(struct sMODBUS));
	handle->pxRxBuff = RingNew(32);

	// Setting della porta - Abilitiamo gli interrupt del timer e il timer stesso
	handle->pxCom = port;
	__HAL_UART_FLUSH_DRREGISTER(port);
	__HAL_UART_CLEAR_IT(port, UART_CLEAR_PEF);
	__HAL_UART_CLEAR_IT(port, UART_CLEAR_FEF);
	__HAL_UART_CLEAR_IT(port, UART_CLEAR_NEF);
	__HAL_UART_CLEAR_IT(port, UART_CLEAR_OREF);
	__HAL_UART_ENABLE_IT(port, UART_IT_RXNE);
	handle->pxCom->Instance->CR1 |= USART_CR1_RTOIE;
	handle->pxCom->Instance->CR2 |= USART_CR2_RTOEN;

	handle->coils.reading = dummyReadingFunction;
	handle->coils.writing = dummyWritingFunction;
	handle->coils.appendData = FrameSlave_AppendCoil;
	handle->coils.readPayload = FrameSlave_ReadCoils;

	handle->discretes.reading = dummyReadingFunction;
	handle->discretes.writing = dummyWritingFunction;
	handle->discretes.appendData = FrameSlave_AppendCoil;
	handle->discretes.readPayload = FrameSlave_ReadCoils;

	handle->holdings.reading = dummyReadingFunction;
	handle->holdings.writing = dummyWritingFunction;
	handle->holdings.appendData = FrameSlave_AppendRegister;
	handle->holdings.readPayload = FrameSlave_ReadRegisters;

	handle->inputs.reading = dummyReadingFunction;
	handle->inputs.writing = dummyWritingFunction;
	handle->inputs.appendData = FrameSlave_AppendRegister;
	handle->inputs.readPayload = FrameSlave_ReadRegisters;

	handle->hwDataTx = dummyTxData;

	q_init(&handle->commands, sizeof(sMODBUS_Commmand), QUEUED_COMMANDS, FIFO, false);

	// I nuovi oggetti MODBUS sono impostati come slave per default
	MODBUS_SetMode(handle, MODBUS_Mode_Slave);

	return handle;
}

/** @brief Libera la memoria occupata dall'oggetto MODBUS
 * @param handle Handle dell'oggetto MODBUS da distruggere
 */
void MODBUS_DeleteHandle(MODBUS_t *handle) {
	// Disattiviamo gli interrupt della USART
	// Cancelliamo gli oggetti interni, poi cancelliamo l'oggetto MODBUS
	handle->pxCom->Instance->RTOR = 0;
	handle->pxCom->Instance->CR1 &= ~USART_CR1_RTOIE;
	handle->pxCom->Instance->CR2 &= ~USART_CR2_RTOEN;

	q_kill(&handle->commands);
	free(handle->pxRxBuff);
	free(handle);
}

INLINE void MODBUS_ExecuteTask(MODBUS_t *handle) {
	handle->task(handle);
}

INLINE void MODBUS_SetAddress(MODBUS_t *handle, uint8_t *address) {
	handle->u8myAddress = address;
}

void MODBUS_SetMode(MODBUS_t *handle, eMODBUS_Mode mode) {
	uint32_t timeout = 0;
	uint32_t setup_ok = 0;

	switch (mode) {
	case MODBUS_Mode_Master:
		setup_ok = 1;
		timeout = MASTER_BITS_TIMEOUT;
		handle->task = MODBUS_MasterTask_WaitAndSendCommand;
		q_flush(&handle->commands);		// Puliamo per sicurezza
		break;

	case MODBUS_Mode_Slave:
		setup_ok = 1;
		timeout = SLAVE_BITS_TIMEOUT;
		handle->task = MODBUS_SlaveTask;
		break;

	default:
		// Modalità non valida: usciamo senza fare nulla
		break;
	}

	// Modalità precedente valida: impostiamo l'hardware
	if (setup_ok) {
		handle->uxMode = mode;
		handle->pxCom->Instance->RTOR = timeout;
	}

}

INLINE void MODBUS_SetWriteCompleteCallback(MODBUS_t *handle, MODBUS_Event writeCmplt) {
	handle->writeCmpltCallback = writeCmplt;
}

INLINE void MODBUS_SetRemoteCmptCallback(MODBUS_t *handle, MODBUS_Event remoteCmplt) {
	handle->remoteRxOKCallback = remoteCmplt;
}

INLINE void MODBUS_SetRemoteErrorCallback(MODBUS_t *handle, MODBUS_Exception remoteError) {
	handle->remoteRxErrorCallback = remoteError;
}

INLINE void MODBUS_SetRxTimeoutCallback(MODBUS_t *handle, MODBUS_Event rxTimeout) {
	handle->rxTimeout = rxTimeout;
}

INLINE void MODBUS_SetHwDataTx(MODBUS_t *handle, MODBUS_DataTx hwDataTx) {
	handle->hwDataTx = hwDataTx;
}

INLINE uint8_t MODBUS_GetMyAddress(const MODBUS_t *handle) {
	return *handle->u8myAddress;
}

INLINE eMODBUS_Mode MODBUS_GetMode(const MODBUS_t *handle) {
	return handle->uxMode;
}

INLINE UART_HandleTypeDef* MODBUS_GetUART(const MODBUS_t *handle) {
	return handle->pxCom;
}

/*
 * COILS' SETTERS
 */
INLINE void MODBUS_Coils_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn) {
	handle->coils.reading = readFn;
}
INLINE void MODBUS_Coils_SetWritingFn(MODBUS_t *handle, MODBUS_LocalWrite writeFn) {
	handle->coils.writing = writeFn;
}
INLINE void MODBUS_Coils_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn) {
	handle->coils.remote = remoteFn;
}

/*
 * DISCRETES' SETTERS
 */
INLINE void MODBUS_Discretes_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn) {
	handle->discretes.reading = readFn;
}
INLINE void MODBUS_Discretes_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn) {
	handle->discretes.remote = remoteFn;
}

/*
 * HOLDINGS' SETTERS
 */
INLINE void MODBUS_Holdings_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn) {
	handle->holdings.reading = readFn;
}
INLINE void MODBUS_Holdings_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn) {
	handle->holdings.remote = remoteFn;
}

/*
 * INPUTS' SETTERS
 */
INLINE void MODBUS_Inputs_SetReadingFn(MODBUS_t *handle, MODBUS_LocalRead readFn) {
	handle->inputs.reading = readFn;
}
INLINE void MODBUS_Inputs_SetWritingFn(MODBUS_t *handle, MODBUS_LocalWrite writeFn) {
	handle->inputs.writing = writeFn;
}
INLINE void MODBUS_Inputs_SetRemoteFn(MODBUS_t *handle, MODBUS_RemoteData remoteFn) {
	handle->inputs.remote = remoteFn;
}

/*
 * Gestione RX
 */
/**
 * @brief Settiamo la flag di ricezione completata.
 * @note Non ha senso farlo in uno stato differente rispetto al Wait dell'RX stessa.
 */
INLINE void MODBUS_SetRxComplete(MODBUS_t *handle) {
	if(handle->task == MODBUS_MasterTask_WaitRx)
		handle->u8RxComplete = 1;
}

INLINE uint8_t MODBUS_GetRxComplete(MODBUS_t *handle) {
	return handle->u8RxComplete;
}

INLINE void MODBUS_SaveByte(MODBUS_t *handle, const uint8_t u8Data) {
	RingAdd(handle->pxRxBuff, u8Data);
}

INLINE void MODBUS_QueueCommand(MODBUS_t *handle, sMODBUS_Commmand *cmd) {
	q_push(&handle->commands, cmd);
}

/**
 * Funzione che gestisce la ricezione dati e l'invio delle risposte quando il MODBUS è in modalità
 * Slave. La funzione non è chiamata direttamente, ma è un metodo interno all'oggetto MODBUS.
 */
void MODBUS_SlaveTask(MODBUS_t *handle) {
	if (handle->u8RxComplete != 1)
		return;
	handle->u8RxComplete = 0;

	sMaster_Frame mFrame;
	sSlave_Frame sFrame;
	eMODBUS_Excpt error = ReadMasterFrame(handle, &mFrame);

	if (error == Exception_NoException) {
		switch (mFrame.u8FuncCode) {
		case FC_WriteMultipleCoils:
			sFrame = WriteMultipleCoils(handle, &mFrame);

			if (handle->writeCmpltCallback != 0)
				handle->writeCmpltCallback();
			break;
		case FC_WriteMultipleRegisters:
			sFrame = WriteMultipleRegisters(handle, &mFrame);

			if (handle->writeCmpltCallback != 0)
				handle->writeCmpltCallback();
			break;

		case FC_ReadCoilStatus:
		case FC_ReadDiscreteInputs:
		case FC_ReadHoldingRegisters:
		case FC_ReadInputRegisters:
			sFrame = ReadValues(handle, &mFrame);
			break;

		case FC_WriteSingleCoil:
		case FC_WriteSingleRegister:
			sFrame = WriteSingle(handle, &mFrame);

			if (handle->writeCmpltCallback != 0)
				handle->writeCmpltCallback();
			break;
		}
	} else {
		sFrame = setupExceptionFrame(&mFrame, error);
	}

	if (error != Exception_InvalidFrame)
		handle->hwDataTx(handle, &sFrame.raw[0], sFrame.u16Length);
}

/**
 * Funzione che gestisce la trasmissione dei dati del MASTER, attende la risposta dallo Slave
 * interrogato e successivamente la elabora. La funzione non è chiamata direttamente,
 * ma è un metodo interno all'oggetto MODBUS.
 */
void MODBUS_MasterTask_WaitAndSendCommand(MODBUS_t *handle) {
	if (q_isEmpty(&handle->commands))
		return;

	q_pop(&handle->commands, &handle->lastCmd);
	sMaster_Frame mFrame = FrameMaster_FromCommand(&handle->lastCmd);

	// Spostato in su per una ragione, ma non ricordo quale... queste due righe devono
	// stare sopra la trasmissione, se no ci sono errori con la sequenza degli stati.
	// Mi pare. Non ricordo con precisione.
	handle->task = MODBUS_MasterTask_WaitRx;
	handle->u16RxTimeout = RX_TIMEOUT_ms;

	handle->hwDataTx(handle, &mFrame.raw[0], mFrame.u16Length);
}


void MODBUS_MasterTask_WaitRx(MODBUS_t *handle) {
	// Ricezione completata con successo
	if (handle->u8RxComplete == 1) {
		handle->u8RxComplete = 0;
		handle->task = MODBUS_MasterTask_ElaborateRx;
	}

	// Timeout della ricezione
	if (handle->u16RxTimeout == 0) {
		FIRE_EVENT(handle->rxTimeout);
		handle->task = MODBUS_MasterTask_WaitAndSendCommand;
		return;
	}
}


void MODBUS_MasterTask_ElaborateRx(MODBUS_t *handle) {
	sSlave_Frame sFrame;
	eMODBUS_Excpt error = ReadSlaveFrame(handle, &sFrame);

	if (error == Exception_NoException) {
		sRegister SelectedReg;
		switch (sFrame.u8FuncCode) {
		case FC_ReadCoilStatus:
			SelectedReg = handle->coils;
			break;
		case FC_ReadDiscreteInputs:
			SelectedReg = handle->discretes;
			break;
		case FC_ReadHoldingRegisters:
			SelectedReg = handle->holdings;
			break;
		case FC_ReadInputRegisters:
			SelectedReg = handle->inputs;
			break;
		}

		for (uint16_t addr = 0; addr < handle->lastCmd.length; addr++) {
			uint16_t data = SelectedReg.readPayload(&sFrame, addr);
			SelectedReg.remote(handle->lastCmd.slaveID, handle->lastCmd.regAddress + addr, data);
		}

		FIRE_EVENT(handle->remoteRxOKCallback);

	} else {
		FIRE_EVENT_1PAR(handle->remoteRxErrorCallback, error);
	}

	handle->task = MODBUS_MasterTask_WaitAndSendCommand;
}

void MODBUS_MasterTickRxTimer(MODBUS_t *handle) {
	if (handle->u16RxTimeout != 0 && handle->task == MODBUS_MasterTask_WaitRx)
		handle->u16RxTimeout--;
}
