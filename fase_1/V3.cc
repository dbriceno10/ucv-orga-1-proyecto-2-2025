#include <iostream> // Biblioteca estándar para entrada y salida (cin, cout)
#include <fstream>  // Biblioteca para manejo de archivos (leer .asm, escribir .txt)
#include <string>   // Biblioteca para manipulación de cadenas de texto
#include <vector>   // Biblioteca para usar vectores (arrays dinámicos)
#include <iomanip>  // Biblioteca para formato de impresión (hex, setw, etc.)
#include <map>      // Biblioteca para usar mapas (diccionarios clave-valor)

using namespace std; // Usamos el espacio de nombres estándar para no escribir std:: a cada rato

// ============================================================================
// --- 1. VARIABLES GLOBALES Y CONFIGURACIÓN ---
// ============================================================================

// Dirección de memoria inicial donde arranca el programa.
// Según el enunciado, se calcula con la suma de las cédulas.
unsigned short DIR_INICIAL = 0x6C94; 

// Bandera para detectar si el código trae una directiva .ORG que cambie el inicio.
bool puntoEntradaFijado = false;

// Tamaño de la memoria simulada: 2^16 = 65536 direcciones (de 0x0000 a 0xFFFF).
const int TAM_MEMORIA = 65536;

// ============================================================================
// --- 2. ESTRUCTURAS DE DATOS ---
// ============================================================================

// Estructura para guardar información de cada línea procesada por el ensamblador.
// Se usa principalmente para generar la tabla final en el archivo output.txt.
struct datosLinea {
    unsigned short dir;           // Dirección de memoria de la instrucción (ej: 0x5000)
    unsigned short codigoMaquina; // El código binario/hexadecimal generado (ej: 0x1234)
    string textoOriginal;         // El texto original del .asm para mostrarlo al lado
    bool esInstruccion;           // true = es código ejecutable; false = es solo espacio o directiva
};

// Estructura para definir el diccionario de instrucciones (ISA)
struct datosInstruccion {
    int opcode; // Código de operación (4 bits)
    int tipo;   // Formato de instrucción: 0=Tipo R, 1=Tipo I, 2=Tipo J
};

// --- ESTRUCTURA DEL PIPELINE (Latches / Registros Intermedios) ---
// Esta estructura es CRÍTICA para el Desafío 2. Representa los "muros" que guardan
// los datos entre una etapa y la siguiente (ej: entre Fetch y Decode).
struct pipelineR {
    bool isNOP = true;          // Indica si es una BURBUJA (instrucción vacía). Si es true, no hace nada.
    unsigned short PCactual;    // Guarda el PC de la instrucción que viaja por la tubería.
    unsigned short instr;       // Guarda el código máquina crudo (16 bits).
    unsigned short PCsiguiente; // Guarda la dirección calculada si es un salto.
    
    // --- Datos Decodificados (Etapa ID) ---
    int opcode, tipo, rd, rs1, rs2, inm;  // Campos extraídos de la instrucción
    int destReg; // Registro destino final (donde se escribirá el resultado en WB)
    
    // --- Valores Leídos (Etapa ID/EX) ---
    short valA; // Valor leído del registro Fuente 1 (Rs1)
    short valB; // Valor leído del registro Fuente 2 (Rs2)
    
    // --- Resultados (Etapa EX/MEM) ---
    short valE; // Resultado calculado por la ALU (Suma, Resta, Dirección, etc.)
    short valM; // Dato leído de la Memoria RAM (para Load Word)

    // --- Señales de Control (Unidad de Control) ---
    // Estas banderas le dicen al hardware qué hacer en cada etapa
    bool MemRead = false;  // Activar lectura de memoria (instrucción LW)
    bool MemWrite = false; // Activar escritura en memoria (instrucción SW)
    bool RegWrite = false; // Activar escritura en registros (ADD, SUB, LW, etc.)
    bool Branch = false;   // Es un salto condicional (BEQ, BNE, BGT)
    bool MemToReg = false; // Multiplexor: 1=Dato viene de RAM, 0=Dato viene de ALU
    bool Halt = false;     // Detener el procesador (instrucción HALT)
    bool Jump = false;     // Es un salto incondicional (JMP, JAL, RET)
    bool ALUSrc = false;   // Multiplexor ALU: 0=Usar Registro B, 1=Usar Inmediato
    
    bool TomarSalto = false; // Bandera interna: indica si la condición del salto se cumplió
};

// Vector global que almacena todas las líneas para el reporte final
vector<datosLinea> listaCodigo;

// Mapa (Diccionario) para la Tabla de Símbolos: Asocia "ETIQUETA" -> Dirección de Memoria
map<string, unsigned short> tablaSimbolos; 

// Memoria temporal usada durante el ensamblado antes de cargarla a la CPU real
short memtemp[TAM_MEMORIA] = {0};

// Definición del Conjunto de Instrucciones (ISA) según el PDF
// Mapea el nombre (string) a sus características (opcode y tipo)
map<string, datosInstruccion> dicInstr = {
    {"ADD", {0, 0}}, {"SUB", {1, 0}}, {"AND", {2, 0}}, {"ORR", {3, 0}},
    {"CMP", {4, 0}}, {"LSL", {5, 0}}, {"LSR", {6, 0}}, {"ASR", {7, 0}},
    {"ADDI",{0, 1}}, {"SUBI",{1, 1}}, {"ANDI",{2, 1}}, {"ORI", {3, 1}},
    {"LW",  {4, 1}}, {"SW",  {5, 1}},
    {"JMP", {0, 2}}, {"BEQ", {1, 2}}, {"BNE", {2, 2}}, {"BGT", {3, 2}},
    {"JAL", {4, 2}}, {"RET", {5, 2}}, {"RETI",{6, 2}}, {"HALT",{7, 2}}
};

// ============================================================================
// --- 3. FUNCIONES UTILITARIAS ---
// ============================================================================

// Función para convertir texto a mayúsculas (facilita comparar "add" con "ADD")
string mayus(string s) {
    for (size_t i = 0; i < s.length(); i++) if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
    return s;
}

// Analiza un string de registro (ej: "R1", "SP") y devuelve su número (0 a 7)
int obtenerRG(string s) {
    if (s.empty()) return 0;
    string temp = mayus(s);
    if (temp == "SP") return 7; // SP es un alias para R7
    if (temp.size() >= 2 && (temp[0] == 'R' || temp[0] == '$')) return stoi(temp.substr(1));
    return 0;
}

// Busca si una etiqueta existe en la tabla de símbolos y devuelve su dirección
int buscarEtiqueta(string etiqueta) {
    if (tablaSimbolos.count(etiqueta)) return tablaSimbolos[etiqueta];
    return -1; // -1 significa que no existe
}

// Convierte texto a número, manejando Decimal, Hexadecimal (0x...) o Etiquetas
int texto_a_numero(string s) {
    if (s.empty()) return 0;
    try {
        // Si empieza con 0x, es hexadecimal
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return stoi(s, nullptr, 16); 
        return stoi(s); // Si no, es decimal
    } catch (...) {
        // Si falla la conversión, asumimos que es una etiqueta y buscamos su dirección
        int dir = buscarEtiqueta(s);
        if (dir != -1) return dir;
        return 0; 
    }
}

// Divide una línea de código en "tokens" (palabras) separadas por espacios o comas
vector<string> separarTokens(string linea) {
    vector<string> tokens;
    string palabra = "";
    for(size_t i = 0; i < linea.length(); i++) {
        char c = linea[i];
        if(c == ',' || c == ' ' || c == '\t') { // Separadores
            if(!palabra.empty()) { tokens.push_back(palabra); palabra = ""; }
        } else { palabra += c; }
    }
    if(!palabra.empty()) tokens.push_back(palabra);
    return tokens;
}

// Limpia una línea: quita comentarios (después de ;) y espacios en blanco extra
string limpieza(string s) {
    size_t posComentario = s.find(';');
    if (posComentario != string::npos) s = s.substr(0, posComentario); // Cortar comentario
    size_t a = s.find_first_not_of(" \t\r\n"); // Buscar primer caracter útil
    if (a == string::npos) return ""; 
    size_t b = s.find_last_not_of(" \t\r\n"); // Buscar último caracter útil
    return s.substr(a, (b - a + 1)); 
}

// ============================================================================
// --- 4. LÓGICA DEL ENSAMBLADOR (TRADUCCIÓN) ---
// ============================================================================

// Procesa una línea de ensamblador y genera código máquina
// Se llama dos veces: Pase 1 (buscar etiquetas) y Pase 2 (generar código)
void procLinea(string linea, unsigned short& dirActual, int pase) {
    linea = limpieza(linea); // Limpiar línea
    if (linea.empty()) return; // Si está vacía, salir

    string lineaLimpia = linea; 

    // --- Manejo de Etiquetas (Terminan en :) ---
    size_t posDosPuntos = linea.find(':');
    if (posDosPuntos != string::npos) {
        string etiqueta = linea.substr(0, posDosPuntos);
        if (pase == 1) tablaSimbolos[etiqueta] = dirActual; // Pase 1: Guardar dirección de etiqueta
        linea = linea.substr(posDosPuntos + 1); // Quitar etiqueta para procesar instrucción
        linea = limpieza(linea); 
    }

    vector<string> partes = separarTokens(linea);
    if (partes.empty()) return; // Si no queda nada, salir

    string nem = mayus(partes[0]); // Obtener nemónico (ADD, SUB, etc.)
    
    // --- Directivas de Ensamblador ---
    if (nem == ".ORG") { // Cambiar origen de memoria
        unsigned short nuevaDir = (unsigned short)texto_a_numero(partes[1]);
        dirActual = nuevaDir;
        if (pase == 1 && !puntoEntradaFijado) { DIR_INICIAL = nuevaDir; puntoEntradaFijado = true; }
        return;
    }
    if (nem == ".SPACE") { // Reservar espacio vacío
        int tam = texto_a_numero(partes[1]);
        if(pase == 2) { 
             datosLinea info = {dirActual, 0, lineaLimpia, false};
             listaCodigo.push_back(info);
        }
        dirActual += tam;
        return;
    }
    if (nem == ".WORD") { // Escribir un número directamente en memoria
        if (pase == 2) {
            unsigned short valor = (unsigned short)texto_a_numero(partes[1]);
            memtemp[dirActual] = (short)valor;
            datosLinea info = {dirActual, valor, lineaLimpia, true}; // Guardar para reporte
            listaCodigo.push_back(info);
        }
        dirActual++;
        return;
    }
    if (nem == ".STRING") { // Escribir cadena de texto char a char
        string contenido = "";
        size_t comillaA = lineaLimpia.find('"');
        size_t comillaB = lineaLimpia.rfind('"');
        if (comillaA != string::npos && comillaB != string::npos && comillaB > comillaA) {
            contenido = lineaLimpia.substr(comillaA + 1, comillaB - comillaA - 1);
        }
        if (pase == 2) {
            for (size_t i = 0; i < contenido.length(); i++) {
                char c = contenido[i];
                memtemp[dirActual] = (short)c;
                string display = (i == 0) ? lineaLimpia : string(1, c);
                datosLinea info = {dirActual, (unsigned short)c, display, true};
                listaCodigo.push_back(info);
                dirActual++;
            }
            memtemp[dirActual] = 0; // Carácter nulo al final
            datosLinea info = {dirActual, 0, "\\0", true};
            listaCodigo.push_back(info);
            dirActual++;
        } else {
            dirActual += contenido.length() + 1;
        }
        return;
    }

    // --- Pseudo-instrucciones (Traducción a instrucciones reales) ---
    if (nem == "MOV") { nem = "ADD"; partes.push_back("R0"); } // MOV R1,R2 -> ADD R1,R2,R0
    else if (nem == "IMOV") { nem = "ADDI"; } // IMOV -> ADDI
    else if (nem == "NOP") { nem = "ADD"; partes.clear(); partes.push_back("ADD"); partes.push_back("R0"); partes.push_back("R0"); partes.push_back("R0"); } // NOP -> ADD R0,R0,R0

    // Buscar instrucción en el diccionario
    unsigned short opcode = 0;
    int tipo = -1;
    if (dicInstr.count(nem)) {
        opcode = dicInstr[nem].opcode;
        tipo = dicInstr[nem].tipo;
    } else { return; } // Instrucción desconocida

    unsigned short maquina = 0;

    // --- Codificación Binaria (Construcción de los 16 bits) ---
    if (tipo == 0) { // Tipo R (Registro-Registro)
        int rd = 0, rs1 = 0, rs2 = 0;
        if (nem == "CMP") { if(partes.size() > 2) { rs1 = obtenerRG(partes[1]); rs2 = obtenerRG(partes[2]); } } 
        else if (nem == "LSL" || nem == "LSR" || nem == "ASR") { if(partes.size() > 3) { rd = obtenerRG(partes[1]); rs1 = obtenerRG(partes[2]); rs2 = texto_a_numero(partes[3]) & 0x7; } } 
        else { if(partes.size() > 3) { rd = obtenerRG(partes[1]); rs1 = obtenerRG(partes[2]); rs2 = obtenerRG(partes[3]); } }
        // Armar bits: Opcode | 00 | Rs1 | Rs2 | Rd
        maquina = (opcode << 12) | (0 << 10) | ((rs1 & 7) << 7) | ((rs2 & 7) << 4) | (rd & 15);
    } 
    else if (tipo == 1) { // Tipo I (Inmediato)
        int rs1 = 0, inm = 0;
        if(partes.size() > 2) { rs1 = obtenerRG(partes[1]); inm = texto_a_numero(partes[2]) & 0x7F; }
        // Armar bits: Opcode | 01 | Rs1 | Inmediato
        maquina = (opcode << 12) | (1 << 10) | ((rs1 & 7) << 7) | inm;
    } 
    else if (tipo == 2) { // Tipo J (Salto)
        int offset = 0;
        if(partes.size() > 1) {
            string etiqueta = partes[1];
            if(pase == 2) {
                int dirDestino = buscarEtiqueta(etiqueta);
                // Calcular salto relativo: Destino - PC Actual
                if(dirDestino != -1) offset = (dirDestino - dirActual) & 0x7F;
            }
        }
        // Armar bits: Opcode | 10 | Offset
        maquina = (opcode << 12) | (2 << 10) | offset;
    }

    // --- Guardar Resultado (Solo en Pase 2) ---
    if (pase == 2) {
        memtemp[dirActual] = (short)maquina; // Escribir en memoria temporal
        datosLinea info = {dirActual, maquina, lineaLimpia, true};
        listaCodigo.push_back(info); // Agregar a la lista para el reporte
    }
    dirActual++; // Avanzar contador de programa
}

// ============================================================================
// --- 5. CLASE CPU (SIMULADOR DE FASE 2) ---
// ============================================================================
class CPU {
private:
    short R[8] = {0};                 // Banco de Registros (R0-R7)
    unsigned short CP;                // Contador de Programa (PC)
    short Memoria[TAM_MEMORIA] = {0}; // Memoria RAM simulada
    
    // Banderas de Estado (PSR)
    bool Z = false, N = false, C = false, V = false; 
    
    // Contadores para métricas de rendimiento
    int ciclos = 0, instrCount = 0;

    // Latches para Pipeline (Desafío 2)
    // F_D: Fetch->Decode, D_E: Decode->Execute, E_M: Execute->Memory, M_WB: Memory->WriteBack
    pipelineR F_D, D_E, E_M, M_WB;

public:
    CPU() { CP = DIR_INICIAL; } // Constructor: Iniciar PC

    // Cargar programa desde memoria temporal a memoria interna de CPU
    void memotemp(short* temp) { for(int i = 0; i < TAM_MEMORIA; i++) Memoria[i] = temp[i]; }

    // =========================================================
    // MODALIDAD 1: MONOCICLO (Fase 2 - Desafio 1)
    // Ejecuta una instrucción completa por iteración del while
    // =========================================================
    void ejecutarMonociclo(ofstream &reporte) {
        ciclos = 0; CP = DIR_INICIAL; instrCount = 0;
        for(int i=0; i<8; i++) R[i]=0; // Limpiar registros
        
        reporte << "\n\n=================================================\n";
        reporte << "      INICIO DE EJECUCION (MONOCICLO)\n";
        reporte << "=================================================\n";

        while (ciclos < 10000) { // Límite de seguridad para evitar bucles infinitos
            if((unsigned short)Memoria[CP] == 0x7800) { // Chequear si es instrucción HALT
                reporte << "\n[Ciclo " << dec << ciclos << "] -> HALT (0x7800) detectado.\n";
                break;
            }
            
            pipelineR pipe; // Estructura temporal para simular etapas
            
            // 1. Fetch: Leer instrucción y avanzar PC
            pipe.PCactual = CP; pipe.instr = (unsigned short)Memoria[CP]; CP++;
            
            // 2. Decode: Extraer campos y leer registros
            pipe.opcode = (pipe.instr >> 12) & 0xF; pipe.tipo = (pipe.instr >> 10) & 0x3;
            // Extracción de operandos según el tipo
            if(pipe.tipo == 0) { pipe.rs1=(pipe.instr>>7)&7; pipe.rs2=(pipe.instr>>4)&7; pipe.rd=pipe.instr&15; pipe.valA=R[pipe.rs1]; pipe.valB=R[pipe.rs2]; }
            else if(pipe.tipo == 1) { pipe.rs1=(pipe.instr>>7)&7; pipe.inm=pipe.instr&127; if(pipe.inm&64)pipe.inm|=0xFF80; pipe.valA=R[pipe.rs1]; }
            else if(pipe.tipo == 2) { pipe.inm=pipe.instr&127; if(pipe.inm&64)pipe.inm|=0xFF80; }
            
            // Unidad de Control: Configurar señales
            pipe.RegWrite = (pipe.tipo!=2 || (pipe.opcode==4 && pipe.tipo==2)) && !(pipe.tipo==0 && pipe.opcode==4) && !(pipe.tipo==1 && pipe.opcode==5);
            pipe.MemRead = (pipe.tipo==1 && pipe.opcode==4); // Solo LW lee memoria
            pipe.MemWrite = (pipe.tipo==1 && pipe.opcode==5); // Solo SW escribe memoria
            pipe.ALUSrc = (pipe.tipo==1); // Tipo I usa inmediato
            pipe.MemToReg = (pipe.tipo==1 && pipe.opcode==4); // Solo LW trae dato de memoria
            pipe.Jump = (pipe.tipo==2 && (pipe.opcode==0 || pipe.opcode==4 || pipe.opcode==5 || pipe.opcode==6));
            pipe.Branch = (pipe.tipo==2 && pipe.opcode>=1 && pipe.opcode<=3);

            // 3. Execute: Operaciones ALU y cálculo de direcciones
            pipe.TomarSalto = false;
            if(pipe.Jump) {
                if(pipe.opcode==4) pipe.valE = pipe.PCactual+1; // JAL guarda PC retorno
                if(pipe.opcode==5 || pipe.opcode==6) pipe.PCsiguiente = R[7]; // RET usa R7
                else pipe.PCsiguiente = pipe.PCactual + pipe.inm; // JMP relativo
                pipe.TomarSalto = true;
            }
            if(pipe.Branch) {
                // Verificar condiciones de banderas (Z, N, V)
                if((pipe.opcode==1 && Z) || (pipe.opcode==2 && !Z) || (pipe.opcode==3 && (!Z && N==V))) {
                    pipe.TomarSalto = true; pipe.PCsiguiente = pipe.PCactual + pipe.inm;
                }
            }
            
            // ALU Lógica Matemática
            int op1 = (int)pipe.valA & 0xFFFF;
            int op2 = (pipe.ALUSrc) ? ((int)pipe.inm & 0xFFFF) : ((int)pipe.valB & 0xFFFF);
            int res = 0;
            switch(pipe.opcode) {
                case 0: res=op1+op2; C=(res>0xFFFF); { bool s1=(short)op1<0, s2=(short)op2<0, sR=(short)res<0; V=(s1==s2)&&(sR!=s1); } break; // ADD
                case 1: case 4: if(pipe.tipo==1 && pipe.opcode==4) res=op1+op2; else { res=op1-op2; C=(op2>op1); bool s1=(short)op1<0, s2=(short)op2<0, sR=(short)res<0; V=(s1!=s2)&&(sR!=s1); } break; // SUB
                case 2: res=op1&op2; break; // AND
                case 3: res=op1|op2; break; // OR
                case 5: if(pipe.tipo==1) res=op1+op2; else { if(pipe.rs2>0) C=(pipe.valA>>(16-pipe.rs2))&1; res=pipe.valA<<(pipe.ALUSrc?pipe.inm:pipe.rs2); } break; // SW/LSL
                case 6: if(pipe.rs2>0) C=(pipe.valA>>(pipe.rs2-1))&1; res=(unsigned short)pipe.valA>>pipe.rs2; break; // LSR
                case 7: if(pipe.rs2>0) C=(pipe.valA>>(pipe.rs2-1))&1; res=pipe.valA>>pipe.rs2; break; // ASR
            }
            pipe.valE = (short)res;
            // Actualizar Banderas solo si corresponde
            if(!pipe.MemRead && !pipe.MemWrite && !pipe.Jump && !pipe.Branch && pipe.opcode!=3) { Z=(pipe.valE==0); N=(pipe.valE<0); }

            // 4. Memory: Acceso a RAM
            if(pipe.MemRead && pipe.valE>=0 && pipe.valE<TAM_MEMORIA) pipe.valM = Memoria[pipe.valE];
            if(pipe.MemWrite && pipe.valE>=0 && pipe.valE<TAM_MEMORIA) Memoria[pipe.valE] = pipe.valA;

            // 5. WriteBack: Escribir en registros
            if(pipe.TomarSalto) CP = pipe.PCsiguiente;
            if(pipe.RegWrite) {
                int regD = (pipe.opcode==4 && pipe.tipo==2) ? 7 : (pipe.tipo==0 ? pipe.rd : pipe.rs1);
                R[regD] = (pipe.MemToReg) ? pipe.valM : pipe.valE;
            }

            // Reporte en formato del Desafío 1 (Estado de registros por ciclo)
            reporte << "\n[Ciclo " << dec << ciclos << "] PC Ejecutado: 0x" << right << hex << uppercase << setw(4) << setfill('0') << pipe.PCactual;
            reporte << " | Instruccion: 0x" << right << setw(4) << setfill('0') << pipe.instr << "\n";
            reporte << "Estado de los Registros:\n";
            for (int i = 0; i < 8; i++) {
                reporte << "R" << i << ": 0x" << right << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << "   ";
                if (i == 3) reporte << "\n";
            }
            reporte << "\n-------------------------------------------------";
            ciclos++; instrCount++;
        }
        imprimirReporteFinal(reporte); // Imprimir resumen final
    }

    // =========================================================
    // MODALIDAD 2: PIPELINE (Fase 2 - Desafio 2)
    // Ejecuta instrucciones solapadas usando Latches
    // =========================================================
    void ejecutarPipeline(ofstream &reporte) {
        ciclos = 0; CP = DIR_INICIAL; instrCount = 0;
        for(int i=0; i<8; i++) R[i]=0;
        
        // Inicializar todas las etapas como BURBUJAS (NOP)
        F_D.isNOP = true; D_E.isNOP = true; E_M.isNOP = true; M_WB.isNOP = true;
        
        reporte << "\n\n=================================================\n";
        reporte << "      INICIO DE EJECUCION (PIPELINE)\n";
        reporte << "=================================================\n";

        // Bucle Principal: 1 iteración = 1 ciclo de reloj del hardware
        while (ciclos < 10000) {
            bool flush = false; // Señal para limpiar pipeline si hubo salto
            bool stall = false; // Señal para detener pipeline si hubo riesgo de datos

            // IMPORTANTE: Ejecutamos las etapas en orden inverso (WB -> IF) 
            // Esto es necesario en simulación de software para que una etapa no use
            // datos que la etapa anterior acaba de generar en el *mismo* ciclo.

            // --- 5. WRITE BACK (Escritura en Registros) ---
            if (!M_WB.isNOP) { // Si hay instrucción válida
                if (M_WB.RegWrite && M_WB.destReg != 0) { // No escribir en R0
                    // Multiplexor: ¿Dato viene de Memoria o de ALU?
                    short dato = (M_WB.MemToReg) ? M_WB.valM : M_WB.valE;
                    R[M_WB.destReg] = dato; // Escritura efectiva
                }
                if (M_WB.Halt) { reporte << "HALT en WB. Fin.\n"; break; } // Terminar si llega HALT al final
                instrCount++;
            }

            // --- 4. MEMORY (Acceso a RAM) ---
            M_WB = E_M; // Mover datos del Latch EX/MEM al MEM/WB
            if (!E_M.isNOP) {
                // Leer o Escribir memoria si las señales de control lo indican
                if (E_M.MemRead && E_M.valE >= 0 && E_M.valE < TAM_MEMORIA) M_WB.valM = Memoria[E_M.valE];
                if (E_M.MemWrite && E_M.valE >= 0 && E_M.valE < TAM_MEMORIA) Memoria[E_M.valE] = E_M.valA;
            }

            // --- 3. EXECUTE (ALU y Cálculo de Direcciones) ---
            E_M = D_E; // Mover datos del Latch ID/EX al EX/MEM
            if (!D_E.isNOP) {
                // Preparar operandos
                int op1 = (int)D_E.valA & 0xFFFF;
                int op2 = (D_E.ALUSrc) ? (D_E.inm & 0xFFFF) : ((int)D_E.valB & 0xFFFF);
                int res = 0;

                // ALU: Switch para operaciones matemáticas (Igual que en monociclo)
                switch(D_E.opcode) {
                    case 0: res = op1 + op2; C=(res>0xFFFF); { bool s1=(short)op1<0,s2=(short)op2<0,sR=(short)res<0; V=(s1==s2)&&(sR!=s1); } break; 
                    case 1: case 4: if(D_E.tipo == 1 && D_E.opcode == 4) res = op1 + op2; else { res = op1 - op2; C=(op2>op1); bool s1=(short)op1<0,s2=(short)op2<0,sR=(short)res<0; V=(s1!=s2)&&(sR!=s1); } break;
                    case 2: res = op1 & op2; break; 
                    case 3: res = op1 | op2; break; 
                    case 5: if(D_E.tipo==1) res=op1+op2; else { if(D_E.rs2>0) C=(D_E.valA>>(16-D_E.rs2))&1; res=D_E.valA<<(D_E.ALUSrc?D_E.inm:D_E.rs2); } break; 
                    case 6: if(D_E.rs2>0) C=(D_E.valA>>(D_E.rs2-1))&1; res=(unsigned short)D_E.valA>>D_E.rs2; break; 
                    case 7: if(D_E.rs2>0) C=(D_E.valA>>(D_E.rs2-1))&1; res=D_E.valA>>D_E.rs2; break; 
                }
                
                E_M.valE = (short)res; // Guardar resultado para siguiente etapa

                // Actualizar Banderas (Solo si no es salto, memoria o HALT)
                if (D_E.opcode != 3 && !D_E.MemRead && !D_E.MemWrite && !D_E.Jump && !D_E.Branch) {
                    Z = (E_M.valE == 0); N = (E_M.valE < 0);
                }

                // Manejo de Saltos (Control Hazards)
                E_M.TomarSalto = false;
                if (D_E.Jump) { // Saltos incondicionales
                    if (D_E.opcode == 4 && D_E.tipo == 2) E_M.valE = D_E.PCactual + 1;
                    if (D_E.opcode == 5 || D_E.opcode == 6) E_M.PCsiguiente = R[7];
                    else E_M.PCsiguiente = D_E.PCactual + D_E.inm;
                    E_M.TomarSalto = true;
                }
                if (D_E.Branch) { // Saltos condicionales
                    bool condicion = false;
                    if (D_E.opcode == 1 && Z) condicion = true;
                    if (D_E.opcode == 2 && !Z) condicion = true;
                    if (D_E.opcode == 3 && (!Z && N == V)) condicion = true;
                    if (condicion) { E_M.TomarSalto = true; E_M.PCsiguiente = D_E.PCactual + D_E.inm; }
                }

                // Si se toma un salto, activamos FLUSH para limpiar las instrucciones incorrectas que vienen detrás
                if (E_M.TomarSalto) { flush = true; CP = E_M.PCsiguiente; }
            }

            // --- 2. DECODE (Decodificación y Detección de Riesgos) ---
            if (!stall) { // Solo avanzar si no hay pausa
                D_E = F_D; // Mover datos del Latch IF/ID al ID/EX
                if (!F_D.isNOP) {
                    // Decodificar campos básicos
                    D_E.opcode = (F_D.instr >> 12) & 0xF;
                    D_E.tipo = (F_D.instr >> 10) & 0x3;
                    
                    // Extraer operandos y definir registro destino
                    if(D_E.tipo == 0) { D_E.rs1=(F_D.instr>>7)&7; D_E.rs2=(F_D.instr>>4)&7; D_E.destReg=F_D.instr&15; D_E.valA=R[D_E.rs1]; D_E.valB=R[D_E.rs2]; } 
                    else if(D_E.tipo == 1) { D_E.rs1=(F_D.instr>>7)&7; D_E.inm=F_D.instr&127; if(D_E.inm&64)D_E.inm|=0xFF80; D_E.destReg = D_E.rs1; if (D_E.opcode == 5) D_E.destReg = 0; D_E.valA=R[D_E.rs1]; } 
                    else if(D_E.tipo == 2) { D_E.inm=F_D.instr&127; if(D_E.inm&64)D_E.inm|=0xFF80; D_E.destReg = (D_E.opcode == 4) ? 7 : 0; }

                    // Detección de Riesgos de Datos (Data Hazards)
                    // Si una instrucción en EX o MEM va a escribir un registro que yo necesito AHORA -> STALL
                    bool riesgoRs1 = (D_E.tipo!=2) && (E_M.RegWrite && E_M.destReg!=0 && E_M.destReg == D_E.rs1);
                    bool riesgoRs2 = (D_E.tipo==0) && (E_M.RegWrite && E_M.destReg!=0 && E_M.destReg == D_E.rs2);
                    bool riesgoMemRs1 = (D_E.tipo!=2) && (M_WB.RegWrite && M_WB.destReg!=0 && M_WB.destReg == D_E.rs1);
                    bool riesgoMemRs2 = (D_E.tipo==0) && (M_WB.RegWrite && M_WB.destReg!=0 && M_WB.destReg == D_E.rs2);

                    if (riesgoRs1 || riesgoRs2 || riesgoMemRs1 || riesgoMemRs2) {
                        stall = true; D_E.isNOP = true; // Insertar Burbuja en EX
                    } 
                    else {
                        // Generar Señales de Control (Si no hay riesgo)
                        D_E.RegWrite = (D_E.tipo!=2 || (D_E.opcode==4 && D_E.tipo==2)) && !(D_E.tipo==0 && D_E.opcode==4) && !(D_E.tipo==1 && D_E.opcode==5);
                        D_E.MemRead = (D_E.tipo==1 && D_E.opcode==4);
                        D_E.MemWrite = (D_E.tipo==1 && D_E.opcode==5);
                        D_E.ALUSrc = (D_E.tipo==1);
                        D_E.MemToReg = (D_E.tipo==1 && D_E.opcode==4);
                        D_E.Jump = (D_E.tipo==2 && (D_E.opcode==0 || D_E.opcode==4 || D_E.opcode==5 || D_E.opcode==6));
                        D_E.Branch = (D_E.tipo==2 && D_E.opcode>=1 && D_E.opcode<=3);
                        D_E.Halt = (D_E.tipo==2 && D_E.opcode==7);
                    }
                }
            }

            // --- 1. FETCH (Búsqueda de Instrucción) ---
            if (!stall) { // Si hay stall, no traemos nueva instrucción (repetimos la actual)
                if (CP < TAM_MEMORIA) { F_D.instr = (unsigned short)Memoria[CP]; F_D.PCactual = CP; F_D.isNOP = false; CP++; } 
                else { F_D.isNOP = true; } // Fin de memoria
            }

            // Si hubo salto, limpiamos Fetch y Decode
            if (flush) { F_D.isNOP = true; D_E.isNOP = true; stall = false; reporte << "   [FLUSH] Salto tomado. Limpiando Pipeline.\n"; }

            // Reporte visual del estado del pipeline
            reporte << "Ciclo " << dec << ciclos << ": ";
            reporte << "IF:" << (stall?"Stall":(flush?"Flush":"OK")) << " ID:" << (D_E.isNOP?"NOP":"OK") << " EX:" << (E_M.isNOP?"NOP":"OK") << " MEM:" << (M_WB.isNOP?"NOP":"OK") << " WB:OK\n";

            ciclos++;
        }
        imprimirReporteFinal(reporte);
    }

    // Función RESTAURADA para imprimir el estado final exactamente como en output.txt original
    void imprimirReporteFinal(ofstream &reporte) {
        reporte << "\nESTADO FINAL DE REGISTROS:\n\n";
        for (int i = 0; i < 8; i++) {
            reporte << "R" << dec << i << ": 0x" << right << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << endl;
        }

        unsigned short psr = 0;
        if (V) psr |= 1; if (C) psr |= 2; if (N) psr |= 4; if (Z) psr |= 8;

        reporte << "PSR: 0x" << right << setw(4) << psr << endl; // Formato original PSR
        
        // Información adicional solicitada por Fase 2
        reporte << "\nMETRICAS:\n";
        reporte << "Ciclos Totales: " << dec << ciclos << endl;
        if (instrCount > 0) reporte << "CPI: " << (float)ciclos/instrCount << endl;
    }
};

// ============================================================================
// --- 6. FUNCIÓN PRINCIPAL (MAIN) ---
// ============================================================================
int main() {
    vector<string> lineas;
    int opcionEntrada, opcionModo;

    // Menú de Entrada
    cout << "Seleccione el modo de entrada:" << endl;
    cout << "1. Leer desde archivo .asm" << endl;
    cout << "2. Escribir codigo en consola" << endl;
    cout << "Opcion: ";
    cin >> opcionEntrada; cin.ignore();

    if (opcionEntrada == 1) {
        string nombreArchivo;
        cout << "Nombre del archivo .asm (sin extension): ";
        cin >> nombreArchivo;
        ifstream archivo(nombreArchivo + ".asm");
        string linea;
        while (getline(archivo, linea)) lineas.push_back(linea);
        archivo.close();
    }
    else if (opcionEntrada == 2) {
        cout << "Ingrese su codigo ensamblador linea por linea" << endl;
        cout << "Escriba 'END' en una nueva linea para terminar y procesar" << endl;
        cout << "--------------------" << endl;
        string linea;
        while (getline(cin, linea)) {
            if (linea == "END") break;
            lineas.push_back(linea);
        }
    }
    else { cout << "Opcion invalida" << endl; return 1; }

    // Selector de MODO (Fase 2)
    cout << "\nSeleccione Modo de Simulacion:\n";
    cout << "1. Monociclo (Fase 2 - Desafio 1)\n";
    cout << "2. Pipeline (Fase 2 - Desafio 2)\n";
    cout << "Opcion: ";
    cin >> opcionModo;

    // Pase 1: Calcular direcciones de etiquetas
    unsigned short dir = DIR_INICIAL;
    for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 1);
    
    // Pase 2: Generar código máquina
    dir = DIR_INICIAL;
    for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 2);
    
    ofstream reporte("output.txt");

    // --- GENERACIÓN DE TABLAS (FORMATO ORIGINAL RESTAURADO) ---
    reporte << "TABLA DE SIMBOLOS:\n\n";
    for (auto &par : tablaSimbolos) {
        reporte << par.first << ": 0x" << hex << uppercase << par.second << endl;
    }

    reporte << "\nCODIGO:\n\n";
    reporte << "-------------------------------------------------\n";
    reporte << "|" << left << setw(10) << " DIR" << "|" << setw(8) << " Hex" << "| Ensamblador               |\n";
    reporte << "-------------------------------------------------\n";

    // Recorremos listaCodigo para imprimir formato exacto
    for (size_t i = 0; i < listaCodigo.size(); i++) {
        if (listaCodigo[i].esInstruccion) {
          reporte << "|" << " 0x" << right << hex << uppercase << setw(4) << setfill('0') << listaCodigo[i].dir << "   | ";
          reporte << right << setw(4) << setfill('0') << listaCodigo[i].codigoMaquina << "   | ";
          reporte << left << setfill(' ') << setw(25) << listaCodigo[i].textoOriginal << " |" << endl;
        }
    }
    reporte << "-------------------------------------------------\n";

    // --- INICIAR CPU Y EJECUCIÓN ---
    CPU procesador;
    procesador.memotemp(memtemp);
    
    if (opcionModo == 1) procesador.ejecutarMonociclo(reporte);
    else procesador.ejecutarPipeline(reporte);

    reporte.close();
    cout << "output.txt generado exitosamente." << endl;
    return 0;
}