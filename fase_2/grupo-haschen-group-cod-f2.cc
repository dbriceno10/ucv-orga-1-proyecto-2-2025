#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <map> 

using namespace std; 

unsigned short DIR_INICIAL = 0x6C94; //suma de las cedulas
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
    bool isNOP = true; //instruccion vacia
    unsigned short PCactual = 0;
    unsigned short instr = 0;
    bool TomarSalto = false;
    unsigned short PCsiguiente = 0;
    int opcode=0, tipo=0, rd=0, rs1=0, rs2=0, inm=0;  
    short valA=0, valB=0, valE=0, valM=0;
    bool MemRead=false, MemWrite=false, RegWrite=false, Branch=false, MemToReg=false, Halt=false, Jump=false, ALUSrc=false; //banderas
    int destReg = 0; 
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

//para separar cada parte de la instruccion
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

//elimina caracteres innecesarios de las linas
string limpieza(string s) {
    size_t posComentario = s.find(';');
    if (posComentario != string::npos) s = s.substr(0, posComentario);
    
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return ""; 
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, (b - a + 1)); 
}

//simula la ejecucion de la linea
void procLinea(string linea, unsigned short& dirActual, int pase) {
    linea = limpieza(linea);
    if (linea.empty()) return;

    string lineaLimpia = linea; 

    size_t posDosPuntos = linea.find(':');	//para encontrar directivas
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

    unsigned short maquina = 0; //almacena el binario de la instruccion de 16 bits

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
    short R[8] = {0}; //R[8] porque van de 0 a 7 los registros
    unsigned short CP; //contador de programa
    short Memoria[TAM_MEMORIA] = {0};
    bool Z = false, N = false, C = false, V = false; 
    int ciclos = 0, instrCount = 0;

    //variables para el pipeline
    pipelineR F_D, D_E, E_M, M_WB; //bandejas de entrada (estado actual leido en este ciclo)
    pipelineR sig_F_D, sig_D_E, sig_E_M, sig_M_WB; //bandejas de salida para el ciclo actual 
    unsigned short sigPC;

public: 
    CPU() {
        CP = DIR_INICIAL;
        sigPC = CP;
        for(int i = 0; i < 8; i++) R[i] = 0;
    }

    void memotemp(short* temp) { //agarra el arreglo con el codigo de maquina y lo copia a la memoria de la CPU
        // justo antes de que empiece la simulación (ciclo 0)
        for(int i = 0; i < TAM_MEMORIA; i++) {
            Memoria[i] = temp[i];
        }
    }
	
  	//ciclo unico sin pipeline
  	
    void fetch(pipelineR &pipe) { //se anota en la bandeja la direccion actual y se lee la memoria en el que esta el codigo maquina y luego aumenta CP para el proximo ciclo
        if(CP >= TAM_MEMORIA) return;
        pipe.PCactual = CP;
        pipe.instr = (unsigned short)Memoria[CP];
        CP++;
    }

    void decode(pipelineR &pipe) { //decodificar en base al tipo y al opcode
        pipe.opcode = (pipe.instr >> 12) & 0xF;
        pipe.tipo = (pipe.instr >> 10) & 0x3;
        
        //datapath para extraer los operandos
        if(pipe.tipo == 0) { 
            pipe.rs1 = (pipe.instr >> 7) & 0x7;
            pipe.rs2 = (pipe.instr >> 4) & 0x7;
            pipe.rd = pipe.instr & 0xF;
            pipe.valA = R[pipe.rs1];
            pipe.valB = R[pipe.rs2];
        } else if(pipe.tipo == 1) { 
            pipe.rs1 = (pipe.instr >> 7) & 0x7;
            pipe.inm = pipe.instr & 0x7F;
            if (pipe.inm & 0x40) pipe.inm |= 0xFF80; 
            pipe.valA = R[pipe.rs1];
        } else if(pipe.tipo == 2) { 
            pipe.inm = pipe.instr & 0x7F;
            if (pipe.inm & 0x40) pipe.inm |= 0xFF80; 
        }
        //inicializar las banderas en false para luego trabajar con ellas
        pipe.RegWrite = false;
        pipe.MemRead = false;
        pipe.MemWrite = false;
        pipe.Branch = false;
        pipe.MemToReg = false;
        pipe.ALUSrc = false;
        pipe.Jump = false;
        pipe.Halt = false;
      
        //unidad de control logica
        if(pipe.tipo == 0) {
            if(pipe.opcode == 4) { //CMP
                pipe.RegWrite = false;
            } else { //ADD, SUB, AND, ORR, LSL, LSR, ASR
                pipe.RegWrite = true;
                pipe.ALUSrc = false;
                pipe.MemToReg = false;
            }
        }
        else if(pipe.tipo == 1) { 
            pipe.ALUSrc = true; //tipo I siempre usa Inmediato
            
            if(pipe.opcode == 4) { //LW
                pipe.MemRead = true;
                pipe.RegWrite = true;
                pipe.MemToReg = true;
            }
            else if (pipe.opcode == 5) { //SW
                pipe.MemWrite = true;
                pipe.RegWrite = false;
            }
            else { //ADDI, SUBI, ANDI, ORI
                pipe.RegWrite = true;
                pipe.MemToReg = false;
            }
        }
        else if (pipe.tipo == 2) {
            if (pipe.opcode == 0) pipe.Jump = true; //JMP
            if (pipe.opcode == 4) { pipe.Jump = true; pipe.RegWrite = true; pipe.MemToReg = false; } //JAL
            if (pipe.opcode == 7) pipe.Halt = true; //HALT
            if (pipe.opcode >= 1 && pipe.opcode <= 3) pipe.Branch = true; //BEQ, BNE, BGT
            if (pipe.opcode == 5 || pipe.opcode == 6) pipe.Jump = true; //RET, RETI
        }
    }

    void execute(pipelineR &pipe) {
        if (pipe.Halt) return;
        pipe.TomarSalto = false;
      
        //calculo de destinos para saltos
        if(pipe.Jump) { //saltos incondicionales
            if(pipe.opcode == 4 && pipe.tipo == 2) { //JAL
                pipe.valE = pipe.PCactual + 1; 
            }
            if(pipe.opcode == 5 || pipe.opcode == 6) pipe.PCsiguiente = R[7]; //RET
            else pipe.PCsiguiente = pipe.PCactual + pipe.inm;
            
            pipe.TomarSalto = true;
        }
      	//saltos condicionales
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

        //ALU
        int op1 = (int)pipe.valA & 0xFFFF;
        int op2 = (pipe.ALUSrc) ? ((int)pipe.inm & 0xFFFF) : ((int)pipe.valB & 0xFFFF);
        int res = 0;    

        switch (pipe.opcode) {
            case 0: //ADD, ADDI
                res = op1 + op2;
                C = (res > 0xFFFF);
                {
                    bool s1 = ((short)op1 < 0); bool s2 = ((short)op2 < 0); bool sR = ((short)res < 0);
                    V = (s1 == s2) && (sR != s1);
                }
                break;
            case 1: //SUB, SUBI
            case 4: //CMP, LW
                if(pipe.tipo == 1 && pipe.opcode == 4) {
                   res = op1 + op2;
                }
                else { //si es SUB, SUBI, CMP es resta
                    res = op1 - op2;
                    C = (op2 > op1);
                    bool s1 = ((short)op1 < 0); bool s2 = ((short)op2 < 0); bool sR = ((short)res < 0);
                    V = (s1 != s2) && (sR != s1);
                }
                break;
            case 2: //AND, ANDI
                res = op1 & op2;
                break;
            case 3: //ORR, ORI
                res = op1 | op2;
                 break;
            case 5: //SW, LSL
                if(pipe.tipo == 1) { //SW
                    res = op1 + op2;
                }
                else { //LSL
                    if (pipe.rs2 > 0) C = (pipe.valA >> (16 - pipe.rs2)) & 1;
                    res = pipe.valA << (pipe.ALUSrc ? pipe.inm : pipe.rs2); 
                }
                break;
            case 6: //LSR
                if(pipe.rs2 > 0) C = (pipe.valA >> (pipe.rs2 - 1)) & 1;
                res = (unsigned short)pipe.valA >> pipe.rs2;
                break;
            case 7: //ASR
                if(pipe.rs2 > 0) C = (pipe.valA >> (pipe.rs2 - 1)) & 1; 
                res = pipe.valA >> pipe.rs2;
                break;
        } 
        
        pipe.valE = (short)res;
		//solo actualiza Z y N en operaciones matemáticas (ignora accesos a memoria, saltos y ORR)
        if(pipe.opcode != 3 && !pipe.MemRead && !pipe.MemWrite && !pipe.Jump && !pipe.Branch) {
            Z = (pipe.valE == 0);
            N = (pipe.valE < 0);
        }    
    }
            
    void memory(pipelineR &pipe) { 
        if (pipe.Halt) return;
        //MemRead se prende solo con LW, leer de la RAM
       	if (pipe.MemRead) {
            if (pipe.valE >= 0 && pipe.valE < TAM_MEMORIA) pipe.valM = Memoria[pipe.valE];
        }
        //MemWrite se prende solo con SW, escribir en la RAM
        if (pipe.MemWrite) {
            if (pipe.valE >= 0 && pipe.valE < TAM_MEMORIA) Memoria[pipe.valE] = pipe.valA; 
        }
    }

    void writeBack(pipelineR &pipe) {
        if (pipe.Halt) return;
        if(pipe.TomarSalto) CP = pipe.PCsiguiente;
        //se saca el valor que puede venir de la memoria o de la ALU para escribirlo en el registro correspondiente
        if(pipe.RegWrite) {
            short dato = (pipe.MemToReg) ? pipe.valM : pipe.valE;
            int Rsiguiente = (pipe.opcode == 4 && pipe.tipo == 2) ? 7 : (pipe.tipo == 0) ? pipe.rd : pipe.rs1;      
            R[Rsiguiente] = dato;                       
        }
    }

   void ejecutarPrograma(ofstream &reporte) {
        ciclos = 0; instrCount = 0;
        reporte << "\nINICIO DE EJECUCION CON CICLO UNICO";

        while (ciclos < 10000) {
            if((unsigned short)Memoria[CP] == 0x7800) {
                reporte << "\n[Ciclo " << dec << ciclos << "] -> HALT (0x7800) detectado. Fin de ejecucion.\n";
                break;
            }
            
            pipelineR pipe; pipe.isNOP = false;
            
           
            fetch(pipe);		//busca la orden en la RAM
            decode(pipe);		//enciende los interruptores de control
            execute(pipe);		//calcula matematicas y saltos
            memory(pipe);		//lee o escribe en la RAM (solo LW/SW)
            writeBack(pipe);	//guarda el resultado en los registro
          
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

	//pipeline
    void ejecutarPipeline(ofstream &reporte) {
      ciclos = 0; instrCount = 0; 
      reporte << "\nINICIO DE EJECUCION CON PIPELINE";
      //se hace en orden inverso las etapas para simular el tiempo correctamente
      while(ciclos<10000){
        if (!M_WB.isNOP) {
            if (M_WB.Halt) {
                reporte << "\n[Ciclo " << dec << ciclos << "]\nHALT detectado en WB\n";
                break;
            }
            
            instrCount++;
            
          	//writeback
            if(M_WB.RegWrite && M_WB.destReg!=0){ 
                short dato = (M_WB.MemToReg)? M_WB.valM : M_WB.valE; 
                R[M_WB.destReg] = dato; 
            }
        }

        bool burbuja = false;
        bool flush = false;
        
        //memory
        sig_M_WB = E_M; //como se usan bandejas la informacion tiene que fluir entre las etapas
        if(!E_M.isNOP){
          if (E_M.MemRead && E_M.valE >= 0 && E_M.valE < TAM_MEMORIA) { //LW
              sig_M_WB.valM = Memoria[E_M.valE]; //aca es sig_M_WB.valM porque en memoria no guarda en los registros, entonces se usa lo de WriteBack porque se encarga de escribir
          }
          if (E_M.MemWrite && E_M.valE >= 0 && E_M.valE < TAM_MEMORIA) {//SW
              Memoria[E_M.valE] = E_M.valA; //no se usa M_WB (para Write Back) porque el SW muere en la ram
          } 
        }
        
        //execute
        sig_E_M = D_E; 
        if(!D_E.isNOP){ 
          int op1 = (int)D_E.valA & 0xFFFF; //op1 siempre sera valA
          int op2 = (D_E.ALUSrc)? (D_E.inm & 0xFFFF) : ((int)D_E.valB & 0xFFFF); //multiplexor (ALUScr) para que dependiendo de las cosas suelte el inmediato o el valor de B (Registro)
          int res = 0; //para trabajar con las operaciones (suma o resta) de op1 y op2 
          
          bool esNOP_real = (D_E.instr == 0x0000);//detecta memoria vacía (0x0000) para evitar que se interprete como ADD R0,R0,R0 y ensucie la bandera Z
          bool actualizaFlags = (!esNOP_real) && ((D_E.tipo == 0) || (D_E.tipo == 1 && D_E.opcode <= 3));//da permiso de escritura al PSR si es una instrucción Tipo R, o Tipo I matemática
          bool nextC = C; bool nextV = V;//borradores para calcular banderas en la ALU sin afectar el estado global

          switch(D_E.opcode){ //para todos los 7 casos que hay dependiendo del opcode que hacer con op1 y op2
              
            case 0: // ADD, ADDI
                    res = op1 + op2; //suma porque es ADD, ADDI
                    nextC = (res > 0xFFFF); //verifica acarreo	
                    { 
                    bool s1 = ((short)op1 < 0); bool s2 = ((short)op2 < 0); bool sR = ((short)res < 0);
                    nextV = (s1 == s2) && (sR != s1); //para la bandera de overflow
                    }
                    break;
              
            case 1: //SUB, SUBI
            case 4: //CMP, LW			
                    if(D_E.tipo == 1 && D_E.opcode == 4) { //tipo I opcode LW
                       res = op1 + op2; 
                    }
                    else if (D_E.tipo == 2 && D_E.opcode == 4) {
        
                    }
                    else { // Si es SUB, SUBI, CMP es resta
                        res = op1 - op2;
                        nextC = (op2 > op1);
                        bool s1 = ((short)op1 < 0); bool s2 = ((short)op2 < 0); bool sR = ((short)res < 0);
                        nextV = (s1 != s2) && (sR != s1);
                    }
                    break;
                
            case 2: //AND, ANDI
                    res = op1 & op2; 
                    break;
            case 3: //ORR, ORI
                    res = op1 | op2; 
                     break;
            case 5: //SW, LSL
                    if(D_E.tipo == 1) { //SW
                        res = op1 + op2; 
                    }
                    else { //LSL
                        if (D_E.rs2 > 0) nextC = (D_E.valA >> (16 - D_E.rs2)) & 1;
                        res = D_E.valA << (D_E.ALUSrc ? D_E.inm : D_E.rs2); 
                    }
                    break;
            case 6: // LSR
                    if(D_E.rs2 > 0) nextC = (D_E.valA >> (D_E.rs2 - 1)) & 1;
                    res = (unsigned short)D_E.valA >> D_E.rs2; 
                    break;
            case 7: // ASR
                    if(D_E.rs2 > 0) nextC = (D_E.valA >> (D_E.rs2 - 1)) & 1; 
                    res = D_E.valA >> D_E.rs2; 
                    break;            
          }     
        
          sig_E_M.valE = (short)res; //guardamos el resultado de la ALU en la bandeja hacia memory
          
          if (actualizaFlags) {
              C = nextC; V = nextV;
              if (D_E.opcode != 3) {
                  Z = (sig_E_M.valE == 0); N = (sig_E_M.valE < 0);
              }
          }
          
          if (D_E.Jump) { //si es salto (incondicional)
            if (D_E.opcode == 4 && D_E.tipo == 2) sig_E_M.valE = D_E.PCactual + 1;                                                             
            if (D_E.opcode == 5 || D_E.opcode == 6) sig_E_M.PCsiguiente = R[7]; 
            else sig_E_M.PCsiguiente = D_E.PCactual + D_E.inm;                                                    
            sig_E_M.TomarSalto = true; 
          }
          
          if (D_E.Branch) { //Branch (salto condicional)
              bool condicion = false; 
              if (D_E.opcode == 1 && Z) condicion = true; 
              if (D_E.opcode == 2 && !Z) condicion = true; 
              if (D_E.opcode == 3 && (!Z && N == V)) condicion = true;
              if (condicion) { sig_E_M.TomarSalto = true; sig_E_M.PCsiguiente = D_E.PCactual + D_E.inm; }                                                                                               
          }                                                                                            
          
          if (sig_E_M.TomarSalto) {
              flush = true;//para informar que se va a otra parte del codigo por los saltos gracias a la ALU
              sigPC = sig_E_M.PCsiguiente; //esto es para que fetch comience en la nueva direccion
          }
        } 
          
        //decode
        sig_D_E = F_D; 
        if (!F_D.isNOP) {
            bool leeRs1 = true; //se asume que casi todas las instrucciones leen Rs1
            bool leeRs2 = (F_D.tipo == 0); //solo las tipo R leen Rs2 de los registros (las demas usan inmediatos) 

            //verificamos si execute o memory están calculando un registro que se necesita leer ahora mismo
            if (leeRs1 && F_D.rs1 != 0) {
                if ((!D_E.isNOP && D_E.RegWrite && D_E.destReg == F_D.rs1) ||
                    (!E_M.isNOP && E_M.RegWrite && E_M.destReg == F_D.rs1)) {
                    burbuja = true; //la etapa de adelante todavia no ha guardado el Rs1 que se necesita y se prende la burbuja para congelar todo
                }
            }
            if (leeRs2 && F_D.rs2 != 0) {
                if ((!D_E.isNOP && D_E.RegWrite && D_E.destReg == F_D.rs2) ||
                    (!E_M.isNOP && E_M.RegWrite && E_M.destReg == F_D.rs2)) {
                    burbuja = true; 
                }
            }

            if (!burbuja) {
                sig_D_E.valA = R[sig_D_E.rs1];
                sig_D_E.valB = R[sig_D_E.rs2];

                sig_D_E.RegWrite = (sig_D_E.tipo == 0 && sig_D_E.opcode != 4) || (sig_D_E.tipo == 1 && sig_D_E.opcode != 5) || (sig_D_E.opcode == 4 && sig_D_E.tipo == 2);
                sig_D_E.ALUSrc = (sig_D_E.tipo == 1);
                sig_D_E.MemRead = (sig_D_E.opcode == 4 && sig_D_E.tipo == 1); 
                sig_D_E.MemWrite = (sig_D_E.opcode == 5 && sig_D_E.tipo == 1); 
                sig_D_E.Jump = (sig_D_E.tipo == 2 && (sig_D_E.opcode == 0 || sig_D_E.opcode == 4 || sig_D_E.opcode == 5 || sig_D_E.opcode == 6)); 
                sig_D_E.Branch = (sig_D_E.tipo == 2 && sig_D_E.opcode >= 1 && sig_D_E.opcode <= 3);
                sig_D_E.MemToReg = sig_D_E.MemRead;
                sig_D_E.Halt = (sig_D_E.tipo == 2 && sig_D_E.opcode == 7);

                //aca se define en que registro vamos a guardar en la etapa Write Back
                if (sig_D_E.RegWrite) {
                    if (sig_D_E.opcode == 4 && sig_D_E.tipo == 2) sig_D_E.destReg = 7; 
                    else if (sig_D_E.tipo == 0) sig_D_E.destReg = sig_D_E.rd; 
                    else sig_D_E.destReg = sig_D_E.rs1; 
                } else sig_D_E.destReg = 0; 
            }
        }
        //si hay un riesgo de control (Flush por salto) o datos (burbuja), vaciamos la bandeja para no ejecutar basura
        if (burbuja || flush) sig_D_E.isNOP = true;
            
        //fetch
        if (burbuja) {
            sig_F_D = F_D; 
        } else if (flush) {
            sig_F_D.isNOP = true;
            CP = sigPC;
        } else {
            if (CP < TAM_MEMORIA) {
                sig_F_D.isNOP = false;
                sig_F_D.PCactual = CP;
                sig_F_D.instr = (unsigned short)Memoria[CP]; //saca la instruccion de la memoria usando el PC
                
                sig_F_D.opcode = (sig_F_D.instr >> 12) & 0xF;
                sig_F_D.tipo = (sig_F_D.instr >> 10) & 0x3;
                
                //limpiamos los registros que la instruccion no usa
                if (sig_F_D.tipo == 0) {
                    sig_F_D.rs1 = (sig_F_D.instr >> 7) & 0x7; sig_F_D.rs2 = (sig_F_D.instr >> 4) & 0x7; sig_F_D.rd = sig_F_D.instr & 0xF;
                } else if (sig_F_D.tipo == 1) {
                    sig_F_D.rs1 = (sig_F_D.instr >> 7) & 0x7; sig_F_D.inm = sig_F_D.instr & 0x7F; if (sig_F_D.inm & 0x40) sig_F_D.inm |= 0xFF80;
                    sig_F_D.rs2 = 0; sig_F_D.rd = 0;
                } else {
                    sig_F_D.inm = sig_F_D.instr & 0x7F; if (sig_F_D.inm & 0x40) sig_F_D.inm |= 0xFF80;
                    sig_F_D.rs1 = 0; sig_F_D.rs2 = 0; sig_F_D.rd = 0; 
                }
                
                sigPC = CP + 1; //avanzamos el PC para el proximo ciclo
            } else sig_F_D.isNOP = true; //si nos pasamos del tamaño maximo de la memoria, inyectamos burbujas
        }

        //para imprimir el estado del ciclo
        reporte << "\n[Ciclo " << dec << ciclos << "]\n";
        
        if (F_D.isNOP) {
            reporte << "Fetch:		NOP\n";
        } else {
            reporte << "Fetch 		0x" << right << hex << uppercase << setw(4) << setfill('0') << F_D.instr 
                    << dec << " (PC:" << F_D.PCactual << ")\n";
        }
        
        reporte << "Decode		: " << (D_E.isNOP ? "NOP" : "En Progreso") << (burbuja ? " (BURBUJA)" : "") << "\n";
        reporte << "Execute		: " << (E_M.isNOP ? "NOP" : "En Progreso") << (flush ? " (FLUSH/SALTO)" : "") << "\n";
        reporte << "Memory		: " << (M_WB.isNOP ? "NOP" : "En Progreso") << "\n";
        reporte << "R0-R7		: ";
        for(int i=0; i<8; i++) reporte << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << " ";
        reporte << "\n----------------------------------------------";

        //pasamos las bandejas para el siguiente ciclo del reloj
        F_D = sig_F_D;
        D_E = sig_D_E;
        E_M = sig_E_M;
        M_WB = sig_M_WB;
        
        //actualizamos el PC global dependiendo de las alarmas
        if (!burbuja && !flush) CP = sigPC;
        else if (flush) CP = sigPC;
        
        ciclos++;
      }
    }

    void imprimirReporte(ofstream &reporte, int modo) {
        reporte << "\n\nESTADO FINAL DE REGISTROS:\n\n";
        for (int i = 0; i < 8; i++) {
            reporte << "R" << dec << i << ": 0x" << right << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << endl;
        }   
        unsigned short psr = 0;
        if (V) psr |= 1; if (C) psr |= 2; if (N) psr |= 4; if (Z) psr |= 8;

        reporte << "PSR: 0x" << right << setw(4) << setfill('0') << psr << endl;
        reporte << "\nMETRICAS DE SIMULACION:\n";
        reporte << "Modo Ejecutado: " << (modo == 1 ? "Ciclo Unico" : "Pipeline") << endl;
        reporte << "Ciclos Totales: " << dec << ciclos << endl;
        reporte << "Instrucciones : " << instrCount << endl;
        
        double cpi = (instrCount > 0) ? (double)ciclos / instrCount : 0;
        reporte << "CPI Promedio  : " << fixed << setprecision(2) << cpi << " ciclos/instruccion" << endl;
    }
};

int main() {
    vector<string> lineas;
    int opcionEntrada, opcionSimulacion;

    cout << "--- ENSAMBLADOR Y SIMULADOR RISC-16 ---" << endl;
    cout << "Seleccione el modo de entrada:" << endl;
    cout << "1. Leer desde archivo .asm" << endl;
    cout << "2. Escribir codigo en consola" << endl;
    cout << "Opcion: ";
    cin >> opcionEntrada;
    cin.ignore();

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
        cout << "Ingrese codigo ensamblador linea por linea ('END' para procesar):" << endl;
        string linea;
        while (getline(cin, linea)) {
            if (linea == "END") break;
            lineas.push_back(linea);
        }
    } else return 1;

    cout << "\nSeleccione el modo de Simulacion de la CPU:" << endl;
    cout << "1. Ciclo Unico" << endl;
    cout << "2. Pipeline" << endl;
    cout << "Opcion: ";
    cin >> opcionSimulacion;

    unsigned short dir = DIR_INICIAL;
    for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 1);
    dir = DIR_INICIAL;
    for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 2);
    
    ofstream reporte("output.txt");

    reporte << "TABLA DE SIMBOLOS:\n\n";
    for (auto &par : tablaSimbolos) reporte << par.first << ": 0x" << hex << uppercase << par.second << endl;

    reporte << "\nCODIGO:\n-------------------------------------------------\n";
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
    
    if (opcionSimulacion == 1) {
        procesador.ejecutarPrograma(reporte);
        procesador.imprimirReporte(reporte, 1);
    } else {
        procesador.ejecutarPipeline(reporte);
        procesador.imprimirReporte(reporte, 2);
    }

    reporte.close();
    cout << "\noutput.txt generado" << endl;
    return 0;
}