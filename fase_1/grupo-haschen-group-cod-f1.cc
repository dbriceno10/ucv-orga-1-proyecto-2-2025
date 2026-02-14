#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <map> 

using namespace std; 

unsigned short DIR_INICIAL = 0x6C94; 
bool puntoEntradaFijado = false;
const int TAM_MEMORIA = 65536;

struct datosLinea {
    unsigned short dir;
    unsigned short codigoMaquina;
    string textoOriginal;
    bool esInstruccion;
};

struct datosInstruccion {
    int opcode;
    int tipo; // 0:R, 1:I, 2:J
};

struct pipelineR {
    unsigned short PCactual;
    unsigned short instr;
    bool TomarSalto;
    unsigned short PCsiguiente;
    int opcode, tipo, rd, rs1, rs2, inm;  
    short valA, valB, valE, valM;
    bool MemRead, MemWrite, RegWrite, Branch, MemToReg, Halt, Jump, ALUSrc;
};

vector<datosLinea> listaCodigo;
map<string, unsigned short> tablaSimbolos; 
short memtemp[TAM_MEMORIA] = {0};

map<string, datosInstruccion> dicInstr = {
    {"ADD", {0, 0}}, {"SUB", {1, 0}}, {"AND", {2, 0}}, {"ORR", {3, 0}},
    {"CMP", {4, 0}}, {"LSL", {5, 0}}, {"LSR", {6, 0}}, {"ASR", {7, 0}},
    
    {"ADDI",{0, 1}}, {"SUBI",{1, 1}}, {"ANDI",{2, 1}}, {"ORI", {3, 1}},
    {"LW",  {4, 1}}, {"SW",  {5, 1}},
    
    {"JMP", {0, 2}}, {"BEQ", {1, 2}}, {"BNE", {2, 2}}, {"BGT", {3, 2}},
    {"JAL", {4, 2}}, {"RET", {5, 2}}, {"RETI",{6, 2}}, {"HALT",{7, 2}}
};

string mayus(string s) {
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] >= 'a' && s[i] <= 'z') s[i] -= 32;
    }
    return s;
}

int obtenerRG(string s) {
    if (s.empty()) return 0;
    string temp = mayus(s);
    if (temp == "SP") return 7;
    if (temp.size() >= 2 && (temp[0] == 'R' || temp[0] == '$')) {
        return stoi(temp.substr(1));
    }
    return 0;
}

int buscarEtiqueta(string etiqueta) {
    if (tablaSimbolos.count(etiqueta)) {
        return tablaSimbolos[etiqueta];
    }
    return -1; 
}

int texto_a_numero(string s) {
    if (s.empty()) return 0;
    try {
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            return stoi(s, nullptr, 16); 
        }
        return stoi(s); 
    } 
    catch (...) {
        int dir = buscarEtiqueta(s);
        if (dir != -1) return dir;
        return 0; 
    }
}

vector<string> separarTokens(string linea) {
    vector<string> tokens;
    string palabra = "";
    for(size_t i = 0; i < linea.length(); i++) {
        char c = linea[i];
        if(c == ',' || c == ' ' || c == '\t') {
            if(!palabra.empty()) {
                tokens.push_back(palabra);
                palabra = "";
            }
        } else {
            palabra += c;
        }
    }
    if(!palabra.empty()) tokens.push_back(palabra);
    return tokens;
}

string limpieza(string s) {
    size_t posComentario = s.find(';');
    if (posComentario != string::npos) s = s.substr(0, posComentario);
    
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return ""; 
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, (b - a + 1)); 
}

void procLinea(string linea, unsigned short& dirActual, int pase) {
    linea = limpieza(linea);
    if (linea.empty()) return;

    string lineaLimpia = linea; 

    size_t posDosPuntos = linea.find(':');
    if (posDosPuntos != string::npos) {
        string etiqueta = linea.substr(0, posDosPuntos);
        if (pase == 1) {
            tablaSimbolos[etiqueta] = dirActual;
        }
        linea = linea.substr(posDosPuntos + 1);
        linea = limpieza(linea); 
    }

    vector<string> partes = separarTokens(linea);
    if (partes.empty()) return;

    string nem = mayus(partes[0]);
    
    //directivas
    if (nem == ".ORG") {
        unsigned short nuevaDir = (unsigned short)texto_a_numero(partes[1]);
        dirActual = nuevaDir;
        
        if (pase == 1 && !puntoEntradaFijado) {
            DIR_INICIAL = nuevaDir;
            puntoEntradaFijado = true;
        }
        return;
    }
    if (nem == ".SPACE") {
        int tam = texto_a_numero(partes[1]);
        if(pase == 2) {
             datosLinea info = {dirActual, 0, lineaLimpia, false};
             listaCodigo.push_back(info);
        }
        dirActual += tam;
        return;
    }
    if (nem == ".WORD") {
        if (pase == 2) {
            unsigned short valor = (unsigned short)texto_a_numero(partes[1]);
            memtemp[dirActual] = (short)valor;
            datosLinea info = {dirActual, valor, lineaLimpia, true};
            listaCodigo.push_back(info);
        }
        dirActual++;
        return;
    }
    if (nem == ".STRING") {
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
            memtemp[dirActual] = 0;
            datosLinea info = {dirActual, 0, "\\0", true};
            listaCodigo.push_back(info);
            dirActual++;
        } else {
            dirActual += contenido.length() + 1;
        }
        return;
    }

    if (nem == "MOV") { nem = "ADD"; partes.push_back("R0"); } 
    else if (nem == "IMOV") { nem = "ADDI"; } 
    else if (nem == "NOP") { nem = "ADD"; partes.clear(); partes.push_back("ADD"); partes.push_back("R0"); partes.push_back("R0"); partes.push_back("R0"); }

    unsigned short opcode = 0;
    int tipo = -1;
    if (dicInstr.count(nem)) {
        opcode = dicInstr[nem].opcode;
        tipo = dicInstr[nem].tipo;
    } else { return; }

    unsigned short maquina = 0;

    if (tipo == 0) { //tipo R
        int rd = 0, rs1 = 0, rs2 = 0;
        if (nem == "CMP") {
            if(partes.size() > 2) { rs1 = obtenerRG(partes[1]); rs2 = obtenerRG(partes[2]); }
        } else if (nem == "LSL" || nem == "LSR" || nem == "ASR") {
            if(partes.size() > 3) { rd = obtenerRG(partes[1]); rs1 = obtenerRG(partes[2]); rs2 = texto_a_numero(partes[3]) & 0x7; }
        } else {
            if(partes.size() > 3) { rd = obtenerRG(partes[1]); rs1 = obtenerRG(partes[2]); rs2 = obtenerRG(partes[3]); }
        }
        maquina = (opcode << 12) | (0 << 10) | ((rs1 & 7) << 7) | ((rs2 & 7) << 4) | (rd & 15);
    } 
    else if (tipo == 1) { //tipo I
        int rs1 = 0, inm = 0;
        if(partes.size() > 2) {
            rs1 = obtenerRG(partes[1]);
            inm = texto_a_numero(partes[2]) & 0x7F; 
        }
        maquina = (opcode << 12) | (1 << 10) | ((rs1 & 7) << 7) | inm;
    } 
    else if (tipo == 2) { //tipo J
        int offset = 0;
        if(partes.size() > 1) {
            string etiqueta = partes[1];
            if(pase == 2) {
                int dirDestino = buscarEtiqueta(etiqueta);
                if(dirDestino != -1) {
                    offset = (dirDestino - dirActual) & 0x7F;
                }
            }
        }
        maquina = (opcode << 12) | (2 << 10) | offset;
    }

    if (pase == 2) {
        memtemp[dirActual] = (short)maquina;
        datosLinea info = {dirActual, maquina, lineaLimpia, true};
        listaCodigo.push_back(info);
    }
    dirActual++;
}

class CPU {
private:
    short R[8] = {0};
    unsigned short CP;
    short Memoria[TAM_MEMORIA] = {0};
    bool Z = false, N = false, C = false, V = false;
    int ciclos = 0, instrCount = 0;

public:
    CPU() {
        CP = DIR_INICIAL;
        for(int i = 0; i < 8; i++) R[i] = 0;
    }

    void memotemp(short* temp) {
        for(int i = 0; i < TAM_MEMORIA; i++) {
            Memoria[i] = temp[i];
        }
    }

    void fetch(pipelineR &pipe) {
        if(CP >= TAM_MEMORIA) return;
        pipe.PCactual = CP;
        pipe.instr = (unsigned short)Memoria[CP];
        CP++;
    }

    void decode(pipelineR &pipe) {
        pipe.opcode = (pipe.instr >> 12) & 0xF;
        pipe.tipo = (pipe.instr >> 10) & 0x3;
        
        // Datapath: Extraer operandos
        if(pipe.tipo == 0) { 
            pipe.rs1 = (pipe.instr >> 7) & 0x7;
            pipe.rs2 = (pipe.instr >> 4) & 0x7;
            pipe.rd = pipe.instr & 0xF;
            pipe.valA = R[pipe.rs1];
            pipe.valB = R[pipe.rs2];
        } else if(pipe.tipo == 1) { 
            pipe.rs1 = (pipe.instr >> 7) & 0x7;
            pipe.inm = pipe.instr & 0x7F;
            if (pipe.inm & 0x40) pipe.inm |= 0xFF80; // Extension de signo
            pipe.valA = R[pipe.rs1];
        } else if(pipe.tipo == 2) { 
            pipe.inm = pipe.instr & 0x7F;
            if (pipe.inm & 0x40) pipe.inm |= 0xFF80; 
        }
      
        pipe.RegWrite = false;
        pipe.MemRead = false;
        pipe.MemWrite = false;
        pipe.Branch = false;
        pipe.MemToReg = false;
        pipe.ALUSrc = false;
        pipe.Jump = false;
        pipe.Halt = false;
      
        // Unidad de control logica
        if(pipe.tipo == 0) {
            if(pipe.opcode == 4) { // CMP
                pipe.RegWrite = false;
            } else { // ADD, SUB, AND, ORR, LSL, LSR, ASR
                pipe.RegWrite = true;
                pipe.ALUSrc = false;
                pipe.MemToReg = false;
            }
        }
        else if(pipe.tipo == 1) { 
            pipe.ALUSrc = true; // Tipo I siempre usa Inmediato
            
            if(pipe.opcode == 4) { // LW
                pipe.MemRead = true;
                pipe.RegWrite = true;
                pipe.MemToReg = true;
            }
            else if (pipe.opcode == 5) { // SW
                pipe.MemWrite = true;
                pipe.RegWrite = false;
            }
            else { // ADDI, SUBI, ANDI, ORI
                pipe.RegWrite = true;
                pipe.MemToReg = false;
            }
        }
        else if (pipe.tipo == 2) {
            if (pipe.opcode == 0) pipe.Jump = true; // JMP
            if (pipe.opcode == 4) { pipe.Jump = true; pipe.RegWrite = true; pipe.MemToReg = false; } // JAL
            if (pipe.opcode == 7) pipe.Halt = true; // HALT
            if (pipe.opcode >= 1 && pipe.opcode <= 3) pipe.Branch = true; // BEQ, BNE, BGT
            if (pipe.opcode == 5 || pipe.opcode == 6) pipe.Jump = true; // RET, RETI
        }
    }

    void execute(pipelineR &pipe) {
        if (pipe.Halt) return;
        pipe.TomarSalto = false;
      
        //calculo de destinos para saltos
        if(pipe.Jump) {
            if(pipe.opcode == 4 && pipe.tipo == 2) { // JAL
                pipe.valE = pipe.PCactual + 1; 
            }
            if(pipe.opcode == 5 || pipe.opcode == 6) pipe.PCsiguiente = R[7]; // RET
            else pipe.PCsiguiente = pipe.PCactual + pipe.inm;
            
            pipe.TomarSalto = true;
        }
        if(pipe.Branch) {
            bool condicion = false;
            if(pipe.opcode == 1 && Z) condicion = true;
            if(pipe.opcode == 2 && !Z) condicion = true;
            if(pipe.opcode == 3 && (!Z && N == V)) condicion = true;
            
            if(condicion) {
                pipe.TomarSalto = true;
                pipe.PCsiguiente = pipe.PCactual + pipe.inm;
            }
        }   

        // ALU
        int op1 = (int)pipe.valA & 0xFFFF;
        int op2 = (pipe.ALUSrc) ? ((int)pipe.inm & 0xFFFF) : ((int)pipe.valB & 0xFFFF);
        int res = 0;    

        switch (pipe.opcode) {
            case 0: // ADD, ADDI
                res = op1 + op2;
                C = (res > 0xFFFF);
                {
                    bool s1 = ((short)op1 < 0); bool s2 = ((short)op2 < 0); bool sR = ((short)res < 0);
                    V = (s1 == s2) && (sR != s1);
                }
                break;
            case 1: // SUB, SUBI
            case 4: // CMP, LW
                if(pipe.tipo == 1 && pipe.opcode == 4) {
                   res = op1 + op2;
                }
                else { // Si es SUB, SUBI, CMP es resta
                    res = op1 - op2;
                    C = (op2 > op1);
                    bool s1 = ((short)op1 < 0); bool s2 = ((short)op2 < 0); bool sR = ((short)res < 0);
                    V = (s1 != s2) && (sR != s1);
                }
                break;
            case 2: // AND, ANDI
                res = op1 & op2;
                break;
            case 3: // ORR, ORI
                res = op1 | op2;
                 break;
            case 5: // SW, LSL
                if(pipe.tipo == 1) { // SW
                    res = op1 + op2;
                }
                else { // LSL
                    if (pipe.rs2 > 0) C = (pipe.valA >> (16 - pipe.rs2)) & 1;
                    res = pipe.valA << (pipe.ALUSrc ? pipe.inm : pipe.rs2); 
                }
                break;
            case 6: // LSR
                if(pipe.rs2 > 0) C = (pipe.valA >> (pipe.rs2 - 1)) & 1;
                res = (unsigned short)pipe.valA >> pipe.rs2;
                break;
            case 7: // ASR
                if(pipe.rs2 > 0) C = (pipe.valA >> (pipe.rs2 - 1)) & 1; 
                res = pipe.valA >> pipe.rs2;
                break;
        } 
        
        pipe.valE = (short)res;

        if(pipe.opcode != 3 && !pipe.MemRead && !pipe.MemWrite && !pipe.Jump && !pipe.Branch) {
            Z = (pipe.valE == 0);
            N = (pipe.valE < 0);
        }    
    }
            
    void memory(pipelineR &pipe) { 
        if (pipe.Halt) return;
        if (pipe.MemRead) {
            if (pipe.valE >= 0 && pipe.valE < TAM_MEMORIA) pipe.valM = Memoria[pipe.valE];
        }
        if (pipe.MemWrite) {
            if (pipe.valE >= 0 && pipe.valE < TAM_MEMORIA) Memoria[pipe.valE] = pipe.valA; 
        }
    }

    void writeBack(pipelineR &pipe) {
        if (pipe.Halt) return;
        if(pipe.TomarSalto) CP = pipe.PCsiguiente;
      
        if(pipe.RegWrite) {
            short dato = (pipe.MemToReg) ? pipe.valM : pipe.valE;
            int Rsiguiente = (pipe.opcode == 4 && pipe.tipo == 2) ? 7 : (pipe.tipo == 0) ? pipe.rd : pipe.rs1;      
            R[Rsiguiente] = dato;                       
        }
    }

   void ejecutarPrograma(ofstream &reporte) {
        ciclos = 0;
        reporte << "\n\n=================================================\n";
        reporte << "      INICIO DE EJECUCION (PASO A PASO)\n";
        reporte << "=================================================\n";

        while (ciclos < 10000) {
            if((unsigned short)Memoria[CP] == 0x7800) {
                reporte << "\n[Ciclo " << dec << ciclos << "] -> HALT (0x7800) detectado. Fin de ejecucion.\n";
                break;
            }
            
            pipelineR pipe; 
            
            fetch(pipe);
            decode(pipe);
            execute(pipe);
            memory(pipe);
            writeBack(pipe);
          
            reporte << "\n[Ciclo " << dec << ciclos << "] PC Ejecutado: 0x" << right << hex << uppercase << setw(4) << setfill('0') << pipe.PCactual;
            reporte << " | Instruccion: 0x" << right << setw(4) << setfill('0') << pipe.instr << "\n";
            reporte << "Estado de los Registros:\n";
            for (int i = 0; i < 8; i++) {
                reporte << "R" << i << ": 0x" << right << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << "   ";
                if (i == 3) reporte << "\n";
            }
            reporte << "\n-------------------------------------------------";
            ciclos++;
            instrCount++;
        }
    }

    void imprimirReporte(ofstream &reporte) {
        reporte << "\nESTADO FINAL DE REGISTROS:\n\n";
        for (int i = 0; i < 8; i++) {
            reporte << "R" << dec << i << ": 0x" << right << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << endl;
        }   
        unsigned short psr = 0;
        if (V) psr |= 1; if (C) psr |= 2; if (N) psr |= 4; if (Z) psr |= 8;

        reporte << "PSR: 0x" << right << setw(4) << psr << endl;
        reporte << "\nMETRICAS DE SIMULACION:\n";
        reporte << "Ciclos Totales: " << dec << ciclos << endl;
        reporte << "Instrucciones:  " << instrCount << endl;
    }
};

int main() {
    vector<string> lineas;
    int opcion;

    cout << "Seleccione el modo de entrada:" << endl;
    cout << "1. Leer desde archivo .asm" << endl;
    cout << "2. Escribir codigo en consola" << endl;
    cout << "Opcion: ";
    cin >> opcion;

    cin.ignore();

    if (opcion == 1) {
        string nombreArchivo;
        cout << "Nombre del archivo .asm (sin extension): ";
        cin >> nombreArchivo;
        ifstream archivo(nombreArchivo + ".asm");

        string linea;
        while (getline(archivo, linea)) lineas.push_back(linea);
        
        archivo.close();
    }
    else if (opcion == 2) {
        cout << "Ingrese su codigo ensamblador linea por linea" << endl;
        cout << "Escriba 'END' en una nueva linea para terminar y procesar" << endl;
        cout << "--------------------" << endl;

        string linea;
        while (getline(cin, linea)) {
            if (linea == "END") break;
            lineas.push_back(linea);
        }
    }
    else {
        cout << "Opcion invalida" << endl;
        return 1;
    }

    // Pase 1
    unsigned short dir = DIR_INICIAL;
    for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 1);
    
    // Pase 2
    dir = DIR_INICIAL;
    for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 2);
    
    ofstream reporte("output.txt");

    reporte << "TABLA DE SIMBOLOS:\n\n";
    for (auto &par : tablaSimbolos) {
        reporte << par.first << ": 0x" << hex << uppercase << par.second << endl;
    }

    reporte << "\nCODIGO:\n\n";
    reporte << "-------------------------------------------------\n";
    reporte << "|" << left << setw(10) << " DIR" << "|" << setw(8) << " Hex" << "| Ensamblador               |\n";
    reporte << "-------------------------------------------------\n";

    for (size_t i = 0; i < listaCodigo.size(); i++) {
        if (listaCodigo[i].esInstruccion) {
          reporte << "|" << " 0x" << right << hex << uppercase << setw(4) << setfill('0') << listaCodigo[i].dir << "   | ";
          reporte << right << setw(4) << setfill('0') << listaCodigo[i].codigoMaquina << "   | ";
          reporte << left << setfill(' ') << setw(25) << listaCodigo[i].textoOriginal << " |" << endl;
        }
    }

    reporte << "-------------------------------------------------\n";

    CPU procesador;
    procesador.memotemp(memtemp);
    procesador.ejecutarPrograma(reporte);
    procesador.imprimirReporte(reporte);

    reporte.close();

    cout << "output.txt generado exitosamente." << endl;
    return 0;
}