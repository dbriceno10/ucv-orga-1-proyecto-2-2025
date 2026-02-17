; ==========================================================
; Archivo: input.asm
; Descripción: Programa de prueba para el procesador RISC-16
; Prueba: Aritmética, Lógica, Saltos, Subrutinas y Pseudo-ops
; ==========================================================

.ORG 20692          ; Dirección de inicio decimal (0x50D4)

; --- 1. Inicialización y Pseudo-Instrucciones ---
NOP                 ; Debe traducirse a ADD R0, R0, R0
IMOV R1, 10         ; R1 = 10 (0x000A)
IMOV R2, 3          ; R2 = 3  (0x0003)
MOV R3, R1          ; R3 = R1 (Copia valor 10 en R3)

; --- 2. Operaciones Aritméticas y Lógicas (Tipo R) ---
ADD R3, R1, R2      ; R3 = 10 + 3 = 13 (0x000D)
SUB R4, R1, R2      ; R4 = 10 - 3 = 7  (0x0007)
AND R5, R1, R2      ; R5 = 10 & 3 (1010 & 0011) = 2 (0x0002)
LSL R6, R2, 2       ; R6 = 3 << 2 (0011 << 2) = 12 (0x000C)

; --- 3. Operaciones con Inmediatos (Tipo I) ---
ADDI R1, 5          ; R1 = 10 + 5 = 15 (0x000F)

; --- 4. Prueba de Bucle (Control de Flujo) ---
; Vamos a usar R4 (que vale 7) como contador hasta llegar a 0
BUCLE:
    CMP R4, R0      ; Compara R4 con 0. Actualiza banderas PSR.
    BEQ FIN_BUCLE   ; Si Z=1 (R4 es 0), salta a FIN_BUCLE
    SUBI R4, 1      ; R4 = R4 - 1
    JMP BUCLE       ; Salta incondicionalmente al inicio

FIN_BUCLE:

; --- 5. Llamada a Subrutina (JAL y RET) ---
    JAL MI_SUBRUTINA ; Guarda PC+1 en R7 y salta
    JMP FINAL        ; Salta al final para terminar

; Definición de la subrutina
MI_SUBRUTINA:
    ADD R2, R2, R2   ; R2 = 3 + 3 = 6
    RET              ; Retorna a la dirección guardada en R7

; --- 6. Terminación ---
FINAL:
    HALT             ; Detiene el procesador

; --- 7. Datos en Memoria (.WORD) ---
; Estos valores se cargarán en memoria justo después del código
.WORD 0xCAFE         ; Dato hexadecimal de prueba
.WORD 999            ; Dato decimal de prueba