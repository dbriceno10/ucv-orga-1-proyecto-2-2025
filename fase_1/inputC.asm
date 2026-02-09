.ORG 16           ; 1. Empieza en dirección 16 (0x0010)
INICIO:           ; 2. Etiqueta
    MOV R1, R2    ; 3. Pseudo-instrucción (Copiar R2 a R1)
    ADDI R1, R1, 5; 4. Sumar 5 a R1
    BEQ FIN       ; 5. Si Z=1, saltar a FIN
    .WORD 65535   ; 6. Datos basura (0xFFFF)
FIN:              ; 7. Etiqueta destino
    HALT          ; 8. Detener