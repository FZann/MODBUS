/*
 * modbus_map.h
 *
 *  Created on: 17 mar 2022
 *      Author: fabizani
 *
 * In questo file sono racchiuse tutte le enum che corrispondono alla MODBUS map poi diffusa ai clienti.
 * Le enum vanno usate nelle funzioni di read/write del MODBUS.
 * Usando le enum possiamo creare delle "catene" di indirizzi: partendo da un indirizzo preciso, gli altri
 * si incrementano da soli di '+ 1' rispetto al precedente.
 * Risulta quindi facile spostare un blocco di indirizzi: basta cambiare il valore della base del blocco.
 *
 * Per completare
 *
 */

#ifndef MODBUS_MODBUS_MAP_1_0_0_H_
#define MODBUS_MODBUS_MAP_1_0_0_H_

/* Il formato della enumerazione è il seguente:
 * --- xxxx_yyyy_zzzz ---
 *
 * Analisi del formato.
 * - xxxx: registri del MODBUS dove il dato è salvato
 * - yyyy: blocco funzionale dei dati
 * - zzzz: nome del dato
 *
 *
 * Per le zone si aggiunge un sottoblocco prima del nome del dato, per specificare il numero della zona.
 */

// NOTA: aderire alla specifica SemVer per il versionamento della mappa!!!
#define MAPVERSION_MAJOR	1
#define MAPVERSION_MINOR	0
#define MAPVERSION_PATCH	0

enum Map_Coils {
	Coils_EmptyGuard,
};

enum Map_Discretes {
	Discretes_EmptyGuard,
};

enum Map_Holdings {
	Holdings_EmptyGuard,
};

enum Map_Inputs {
	Inputs_EmptyGuard,
};

#endif /* MODBUS_MODBUS_MAP_1_0_0_H_ */
