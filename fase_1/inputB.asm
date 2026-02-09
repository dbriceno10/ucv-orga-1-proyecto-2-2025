.ORG 0x100

; ==========================================
; PRUEBA ROBUSTA DE BANDERAS (V, C, Z, N)
; ==========================================

; 1. Construir el valor 0x8000 (-32768) en R2
; Como no podemos usar IMOV con numeros grandes, 
; lo hacemos con desplazamientos (Shifts).

IMOV R2, 1      ; R2 = 1
LSL R2, R2, 7   ; R2 = 128 (0x0080)
LSL R2, R2, 7   ; R2 = 16384 (0x4000)
LSL R2, R2, 1   ; R2 = 32768 (0x8000) -> Signo negativo activado

; A este punto R2 tiene 0x8000 (Binario: 1000...0000)

; 2. EL GRAN FINAL: Sumar R2 + R2
; Operación: 0x8000 + 0x8000
; Matemáticamente: (-32768) + (-32768) = -65536
; En 16 bits:      0x10000 -> Se corta a 0x0000

ADD R2, R2, R2

; ANÁLISIS DEL RESULTADO ESPERADO:
; - Resultado 0x0000  -> Z=1 (Zero)
; - Bit 16 es 1       -> C=1 (Carry)
; - Neg + Neg = Pos   -> V=1 (Overflow)
; - Resultado no Neg  -> N=0

HALT