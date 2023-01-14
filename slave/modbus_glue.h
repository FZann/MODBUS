/*
 * modbus_glue.h
 *
 *  Created on: 10 mar 2022
 *      Author: fabizani
 *
 *  Questo header file serve per racchiudere quelle dichiarazioni che servono all'applicazione
 *  per poter usare propriamente la libreria MODBUS.
 *
 *  Deve essere scritto dall'utente, dichiarando ciò che gli servirà nell'app.
 *  Questo serve per evitare modifiche alla libreria MODBUS vera e propria, mettendo in un
 *  punto unico il codice che potrebbe subire modifiche in base alle esigenze dell'applicazione.
 */

#ifndef MODBUS_MODBUS_SLAVE_H_
#define MODBUS_MODBUS_SLAVE_H_


#include <Core/modbus_core.h>

// Variabile MODBUS e relative funzioni di callback
extern MODBUS_t* MODBUS;

void MODBUS_Init();


sMODBUS_ReadResult MODBUS_readCoils(const uint16_t address);
eMODBUS_Excpt MODBUS_writeCoils(const uint16_t address, const uint16_t data);
void MODBUS_remoteCoils(const uint8_t ID, const uint16_t address, const uint16_t data);

sMODBUS_ReadResult MODBUS_readDiscretes(const uint16_t address);
void MODBUS_remoteDiscretes(const uint8_t ID, const uint16_t address, const uint16_t data);

sMODBUS_ReadResult MODBUS_readHoldings(const uint16_t address);
void MODBUS_remoteHoldings(const uint8_t ID, const uint16_t address, const uint16_t data);

sMODBUS_ReadResult MODBUS_readInputs(const uint16_t address);
eMODBUS_Excpt MODBUS_writeInputs(const uint16_t address, const uint16_t data);
void MODBUS_remoteInputs(const uint8_t ID, const uint16_t address, const uint16_t data);

//************************ CALLBACKS ************************
void MODBUS_WriteCallback();
void MODBUS_HwDataTx(const MODBUS_t *handle, const uint8_t *data, const uint8_t len);

#endif /* MODBUS_MODBUS_SLAVE_H_ */
