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
    bool salto;
    unsigned short PCsiguiente;
    int opcode, tipo, rd, rs1,rs2, inm;  
    short valA, valB, valE, valM;      
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
    for (int i = 0; i < s.length(); i++) {
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

class CPU{
    private:
        short R[8]={0};
        unsigned short CP=DIR_INICIAL;
        short Memoria[TAM_MEMORIA]={0};
        bool Z = false, N = false, C = false, V = false;
        int ciclos=0, instr=0;
  
    public:
        void memotemp(short* temp){
            for(int i=0; i<TAM_MEMORIA; i++){
            Memoria[i] = temp[i];
            }
        }
        void fetch(pipelineR &pipe){
        }
        void decode(pipelineR &pipe){
        }

        void execute(pipelineR &pipe){
        }

        void memory(pipelineR &pipe){
        }

        void writeBack(pipelineR &pipe){
        }
        void ejecutarPrograma(){
        }
        void imprimirReporte(ofstream &reporte) {
            reporte << "\nESTADO FINAL DE REGISTROS:\n\n";
            for (int i = 0; i < 8; i++) {
                reporte << "R" << dec << i << ": 0x" << right << hex << uppercase << setw(4) << setfill('0') << (unsigned short)R[i] << endl;
            }   
        }
};

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
        maquina = (opcode << 12) | (0 << 10) | ((rs1 & 7) << 7) | ((rs2 & 7) << 4) | (rd & 15); //construyendo el codigo maquina de la instruccion desplazando cada parte en su lugar
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

void simular() {
    CP = DIR_INICIAL;
    int ciclos = 0;
    
    for(int i=0; i<8; i++) R[i] = 0;
    Z = false; N = false; C = false; V = false;

    while (ciclos < 10000) {
        if(CP >= TAM_MEMORIA) break;
        unsigned short instr = (unsigned short)Memoria[CP];
        if (instr == 0x7800) break;

        int op = (instr >> 12) & 0xF;
        int tipo = (instr >> 10) & 0x3;
        unsigned short sigCP = CP + 1;

        if (tipo == 0) { //tipo R
            int rs1 = (instr >> 7) & 0x7;
            int rs2 = (instr >> 4) & 0x7;
            int rd = instr & 0xF;
            short val1 = R[rs1];
            short val2 = R[rs2];
            short res = 0;

            if (op == 0) { //add
                int v1 = (int)val1 & 0xFFFF;
                int v2 = (int)val2 & 0xFFFF;
                int sumaTotal = v1 + v2;
                res = (short)(sumaTotal & 0xFFFF);
                C = (sumaTotal > 0xFFFF); 
                bool s1 = ((short)val1 < 0); bool s2 = ((short)val2 < 0); bool sR = (res < 0);
                V = (s1 == s2) && (sR != s1);
            }
            else if (op == 1 || op == 4) { //sub y cmp
                int v1 = (int)val1 & 0xFFFF;
                int v2 = (int)val2 & 0xFFFF;
                int restaTotal = v1 - v2;
                res = (short)(restaTotal & 0xFFFF);
                C = (v2 > v1); 
                bool s1 = ((short)val1 < 0); bool s2 = ((short)val2 < 0); bool sR = (res < 0);
                V = (s1 != s2) && (sR != s1);
            }
            else if (op == 2) res = val1 & val2;
            else if (op == 3) res = val1 | val2;
            else if (op == 5) { //lsl
                if (rs2 > 0) C = (val1 >> (16 - rs2)) & 1;
                res = val1 << rs2;
            }
            else if (op == 6) { //lsr casteando a unsigned para que el desplazamiento llene con ceros
                if (rs2 > 0) C = (val1 >> (rs2 - 1)) & 1;
                res = (unsigned short)val1 >> rs2;
            }
            else if (op == 7) { //asr
                if (rs2 > 0) C = (val1 >> (rs2 - 1)) & 1;
                res = val1 >> rs2;
            }

            if (op != 4) R[rd] = (short)res;
            if (op != 3) { Z = ((short)res == 0); N = ((short)res < 0); }
        } 
        else if (tipo == 1) { //tipo I
            int rs1 = (instr >> 7) & 0x7;
            int inm = instr & 0x7F;
            if (inm & 0x40) inm |= 0xFF80;

            if (op == 0) { //addi
                int v1 = (int)R[rs1] & 0xFFFF;
                int v2 = (int)inm & 0xFFFF; 
                int sumaTotal = v1 + v2;
                short res = (short)(sumaTotal & 0xFFFF);
                C = (sumaTotal > 0xFFFF);
                bool s1 = (R[rs1] < 0); bool s2 = (inm < 0); bool sR = (res < 0);
                V = (s1 == s2) && (sR != s1);
                R[rs1] = res;
                Z = (res == 0); N = (res < 0);
            }
            else if (op == 1) { //subi
                int v1 = (int)R[rs1] & 0xFFFF;
                int v2 = (int)inm & 0xFFFF;
                int restaTotal = v1 - v2;
                short res = (short)(restaTotal & 0xFFFF);
                C = (v2 > v1);
                bool s1 = (R[rs1] < 0); bool s2 = (inm < 0); bool sR = (res < 0);
                V = (s1 != s2) && (sR != s1);
                R[rs1] = res;
                Z = (res == 0); N = (res < 0);
            }
            else if (op == 4) { int addr = R[rs1] + inm; if(addr < TAM_MEMORIA) R[rs1] = Memoria[addr]; }
            else if (op == 5) { int addr = R[rs1] + inm; if(addr < TAM_MEMORIA) Memoria[addr] = R[rs1]; }
        }
        else if (tipo == 2) { //tipo J
            int offset = instr & 0x7F;
            if (offset & 0x40) offset |= 0xFF80;
            unsigned short destino = CP + offset;
            if (op == 0) sigCP = destino; 
            else if (op == 1) { if(Z) sigCP = destino; } 
            else if (op == 2) { if(!Z) sigCP = destino; } 
            else if (op == 3) { if(!Z && (N == V)) sigCP = destino; } 
            else if (op == 4) { R[7] = CP + 1; sigCP = destino; } 
            else if (op == 5) { sigCP = R[7]; }
			else if (op == 6) { sigCP = R[7]; }
            else if (op == 7) { ciclos = 99999; } 
        }
        CP = sigCP;
        ciclos++;
    }
}

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

	//pase 1
	unsigned short dir = DIR_INICIAL;
	for (size_t i = 0; i < lineas.size(); i++) procLinea(lineas[i], dir, 1);
	
	//pase 2
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

	simular();

	unsigned short psr = 0;
	if (V) psr |= 1; if (C) psr |= 2; if (N) psr |= 4; if (Z) psr |= 8;

	reporte << "PSR: 0x" << right << setw(4) << psr;
	reporte.close();

	cout << "output.txt generado." << endl;
	return 0;
}