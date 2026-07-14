#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <BluetoothSerial.h>

// ==================== CONFIGURACIÓN DE PINES ====================
LiquidCrystal_I2C lcd(0x27, 20, 4);

#define RFID_SS     15
#define RFID_RST    -1
#define RFID_SCK    14
#define RFID_MISO   12
#define RFID_MOSI   13
MFRC522 rfid(RFID_SS, RFID_RST);

#define SD_CS       5
#define SD_SCK      18
#define SD_MISO     19
#define SD_MOSI     23
SPIClass SPI_HSPI(HSPI);

const byte FILAS = 4, COLUMNAS = 4;
byte pinesFila[FILAS] = {25, 26, 27, 32};
byte pinesColumna[COLUMNAS] = {34, 35, 36, 39};
char teclado[FILAS][COLUMNAS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

BluetoothSerial SerialBT;
bool btConectado = false;

// ==================== VARIABLES DE ESTADO ====================
bool primeraTarjeta = false;
String usuarioActualUID = "";
int usuarioActualRol = 0;
String usuarioActualNombre = "";

// ---- TIMEOUT ----
unsigned long ultimoTiempoActividad = 0;

// ==================== COMPONENTES EN MEMORIA ====================
#define MAX_COMPONENTES 250
int numComponentes = 0;
int idComponentes[MAX_COMPONENTES];
String nombreComponentes[MAX_COMPONENTES];
int stockComponentes[MAX_COMPONENTES];
String uidComponentes[MAX_COMPONENTES];

// ==================== RTC DS1302 ====================
#define RTC_CLK   17
#define RTC_DAT   16
#define RTC_RST   4

struct DateTime {
  int year, month, day, hour, minute, second;
};

uint8_t dec2bcd(uint8_t dec) { return ((dec/10)<<4) | (dec%10); }
uint8_t bcd2dec(uint8_t bcd) { return ((bcd>>4)*10) + (bcd&0x0F); }

void DS1302_writeByte(uint8_t data) {
  pinMode(RTC_DAT, OUTPUT);
  for(int i=0; i<8; i++) {
    digitalWrite(RTC_DAT, (data&0x01) ? HIGH : LOW);
    delayMicroseconds(10);
    digitalWrite(RTC_CLK, HIGH);
    delayMicroseconds(10);
    digitalWrite(RTC_CLK, LOW);
    data >>= 1;
  }
}

uint8_t DS1302_readByte() {
  pinMode(RTC_DAT, INPUT);
  uint8_t data = 0;
  for(int i=0; i<8; i++) {
    data >>= 1;
    if(digitalRead(RTC_DAT)) data |= 0x80;
    digitalWrite(RTC_CLK, HIGH);
    delayMicroseconds(10);
    digitalWrite(RTC_CLK, LOW);
    delayMicroseconds(10);
  }
  return data;
}

void DS1302_writeRegister(uint8_t reg, uint8_t value) {
  noInterrupts();
  digitalWrite(RTC_RST, LOW);
  delayMicroseconds(10);
  digitalWrite(RTC_RST, HIGH);
  delayMicroseconds(10);
  DS1302_writeByte(reg);
  DS1302_writeByte(value);
  digitalWrite(RTC_RST, LOW);
  interrupts();
}

uint8_t DS1302_readRegister(uint8_t reg) {
  noInterrupts();
  digitalWrite(RTC_RST, LOW);
  delayMicroseconds(10);
  digitalWrite(RTC_RST, HIGH);
  delayMicroseconds(10);
  DS1302_writeByte(reg | 0x01);
  uint8_t value = DS1302_readByte();
  digitalWrite(RTC_RST, LOW);
  interrupts();
  return value;
}

DateTime DS1302_getTime() {
  DateTime dt;
  uint8_t sec  = DS1302_readRegister(0x81);
  uint8_t min  = DS1302_readRegister(0x83);
  uint8_t hr   = DS1302_readRegister(0x85);
  uint8_t date = DS1302_readRegister(0x87);
  uint8_t mon  = DS1302_readRegister(0x89);
  uint8_t yr   = DS1302_readRegister(0x8D);
  dt.second = bcd2dec(sec & 0x7F);
  dt.minute = bcd2dec(min);
  dt.hour   = bcd2dec(hr & 0x3F);
  dt.day    = bcd2dec(date);
  dt.month  = bcd2dec(mon);
  dt.year   = 2000 + bcd2dec(yr);
  return dt;
}

void DS1302_setTime(const DateTime &dt) {
  DS1302_writeRegister(0x8E, 0x00);
  DS1302_writeRegister(0x80, 0x80);
  DS1302_writeRegister(0x82, dec2bcd(dt.minute));
  DS1302_writeRegister(0x84, dec2bcd(dt.hour));
  DS1302_writeRegister(0x86, dec2bcd(dt.day));
  DS1302_writeRegister(0x88, dec2bcd(dt.month));
  DS1302_writeRegister(0x8C, dec2bcd(dt.year - 2000));
  DS1302_writeRegister(0x80, dec2bcd(dt.second) & 0x7F);
  DS1302_writeRegister(0x8E, 0x80);
}

void DS1302_init() {
  pinMode(RTC_RST, OUTPUT);
  pinMode(RTC_CLK, OUTPUT);
  digitalWrite(RTC_RST, LOW);
  digitalWrite(RTC_CLK, LOW);
  uint8_t sec = DS1302_readRegister(0x81);
  if (sec & 0x80) {
    DateTime dt = {2026, 5, 5, 12, 0, 0};
    DS1302_setTime(dt);
  }
}

String obtenerFechaHora() {
  DateTime dt = DS1302_getTime();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  return String(buf);
}

String obtenerFechaHoy() {
  DateTime dt = DS1302_getTime();
  char buf[11];
  sprintf(buf, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
  return String(buf);
}

// ==================== FUNCIONES DE TECLADO Y LCD ====================
char obtenerTecla() {
  for (int f = 0; f < FILAS; f++) {
    pinMode(pinesFila[f], OUTPUT);
    digitalWrite(pinesFila[f], HIGH);
  }
  for (int c = 0; c < COLUMNAS; c++) {
    pinMode(pinesColumna[c], INPUT);
  }
  for (int f = 0; f < FILAS; f++) {
    digitalWrite(pinesFila[f], LOW);
    delayMicroseconds(20);
    for (int c = 0; c < COLUMNAS; c++) {
      if (digitalRead(pinesColumna[c]) == LOW) {
        delay(80);
        if (digitalRead(pinesColumna[c]) == LOW) {
          while (digitalRead(pinesColumna[c]) == LOW);
          digitalWrite(pinesFila[f], HIGH);
          ultimoTiempoActividad = millis();
          return teclado[f][c];
        }
      }
      if (millis() - ultimoTiempoActividad > 20000) {
        digitalWrite(pinesFila[f], HIGH);
        return 'T';
      }
    }
    digitalWrite(pinesFila[f], HIGH);
  }
  return 0;
}

char mostrarMenu4(String l1, String l2, String l3, String l4) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
  lcd.setCursor(0, 2); lcd.print(l3);
  lcd.setCursor(0, 3); lcd.print(l4);
  ultimoTiempoActividad = millis();
  while (true) {
    char tecla = obtenerTecla();
    if (tecla == 'A' || tecla == 'B' || tecla == 'C' || tecla == 'D' || tecla == '*' || tecla == 'T') {
      return tecla;
    }
  }
}

void mostrarMensaje(String l1, String l2, int t = 2000) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
  delay(t);
  ultimoTiempoActividad = millis();
}

void mostrarListaNavegable(String contenido, String titulo) {
  int total = 0;
  for (int i = 0; i < contenido.length(); i++) {
    if (contenido[i] == '\n') total++;
  }
  if (total == 0) {
    mostrarMensaje("Sin datos", "", 1500);
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(titulo);
  delay(10);

  int offset = 0;
  int anteriorOffset = -1;
  ultimoTiempoActividad = millis();
  while (true) {
    if (offset != anteriorOffset) {
      anteriorOffset = offset;
      int inicio = 0;
      for (int i = 0; i < offset; i++) {
        inicio = contenido.indexOf('\n', inicio) + 1;
      }
      for (int i = 0; i < 2; i++) {
        lcd.setCursor(0, i + 1);
        lcd.print("                    ");
        lcd.setCursor(0, i + 1);
        int fin = contenido.indexOf('\n', inicio);
        if (fin != -1) {
          String linea = contenido.substring(inicio, fin);
          lcd.print(linea.substring(0, 20));
          inicio = fin + 1;
        } else break;
      }
      lcd.setCursor(0, 3);
      lcd.print("                    ");
      lcd.setCursor(0, 3);
      if (offset > 0 && offset + 2 < total) lcd.print("A:Arr B:Abj C:Salir");
      else if (offset > 0) lcd.print("A:Arr C:Salir       ");
      else if (offset + 2 < total) lcd.print("B:Abj C:Salir       ");
      else lcd.print("C:Salir              ");
    }
    char tecla = obtenerTecla();
    if (tecla == 'T') return;
    if (tecla == 'A' && offset > 0) offset--;
    else if (tecla == 'B' && offset + 2 < total) offset++;
    else if (tecla == 'C' || tecla == '*') break;
  }
}

// ==================== MANEJO DE ARCHIVOS ====================
void escribirArchivo(String nombre, String contenido, bool append = true) {
  File f = SD.open(nombre, append ? FILE_APPEND : FILE_WRITE);
  if (f) { f.println(contenido); f.close(); }
}

String leerLinea(File &f) {
  String linea = "";
  while (f.available()) {
    char c = f.read();
    if (c == '\n') break;
    linea += c;
  }
  linea.trim();
  return linea;
}

void enviarArchivoFiltrado(File &f) {
  const int BUF_SIZE = 1024;
  char buf[BUF_SIZE];
  int len;
  char salida[512];
  int posSalida = 0;
  bool nuevaLinea = true;  // Indica si estamos al inicio de una línea

  while ((len = f.read((uint8_t*)buf, BUF_SIZE)) > 0) {
    for (int i = 0; i < len; i++) {
      char c = buf[i];
      if (c == '\n' || c == '\r') {
        if (!nuevaLinea) {
          // Final de una línea no vacía: añadir '\n' y marcar nueva línea
          salida[posSalida++] = '\n';
          if (posSalida >= sizeof(salida) - 1) {
            SerialBT.write((uint8_t*)salida, posSalida);
            posSalida = 0;
          }
          nuevaLinea = true;
        }
        // Si ya estamos en nueva línea, ignoramos este salto extra (línea vacía)
      } else {
        if (nuevaLinea) {
          nuevaLinea = false;
        }
        if (c == ',') c = '|';
        salida[posSalida++] = c;
        if (posSalida >= sizeof(salida) - 1) {
          SerialBT.write((uint8_t*)salida, posSalida);
          posSalida = 0;
        }
      }
    }
  }
  if (posSalida > 0) {
    SerialBT.write((uint8_t*)salida, posSalida);
  }
}

// ==================== NORMALIZACIÓN DE UID ====================
String normalizarUID(String uid) {
  String norm = "";
  for (int i = 0; i < uid.length(); i++) {
    char c = uid[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) norm += c;
    else if (c >= 'a' && c <= 'f') norm += (c - 32);
  }
  return norm;
}

// ==================== GESTIÓN DE USUARIOS ====================
String buscarUsuario(String uidBuscado) {
  File f = SD.open("/usuarios.txt", FILE_READ);
  if (!f) return "";
  String uidNorm = normalizarUID(uidBuscado);
  while (f.available()) {
    String linea = leerLinea(f);
    int pos = linea.indexOf(',');
    if (pos > 0) {
      String uidLinea = normalizarUID(linea.substring(0, pos));
      if (uidLinea == uidNorm) {
        f.close();
        return linea.substring(pos + 1);
      }
    }
  }
  f.close();
  return "";
}

void agregarUsuario(String uid, String nombre, int rol) {
  escribirArchivo("/usuarios.txt", uid + "," + String(rol) + "," + nombre);
}

bool eliminarUsuario(String uidBuscado) {
  File f = SD.open("/usuarios.txt", FILE_READ);
  if (!f) return false;
  String nuevo = "";
  bool encontrado = false;
  String uidNorm = normalizarUID(uidBuscado);
  while (f.available()) {
    String linea = leerLinea(f);
    int pos = linea.indexOf(',');
    if (pos > 0) {
      String uidLinea = normalizarUID(linea.substring(0, pos));
      if (uidLinea == uidNorm) {
        encontrado = true;
        continue;
      }
    }
    nuevo += linea + "\n";
  }
  f.close();
  if (encontrado) {
    SD.remove("/usuarios.txt");
    escribirArchivo("/usuarios.txt", nuevo, false);
  }
  return encontrado;
}

bool modificarNombreUsuario(String uidBuscado, String nuevoNombre) {
  File f = SD.open("/usuarios.txt", FILE_READ);
  if (!f) return false;
  String nuevo = "";
  bool encontrado = false;
  String uidNorm = normalizarUID(uidBuscado);
  while (f.available()) {
    String linea = leerLinea(f);
    int pos = linea.indexOf(',');
    if (pos > 0) {
      String uidLinea = normalizarUID(linea.substring(0, pos));
      if (uidLinea == uidNorm) {
        int p1 = pos;
        int p2 = linea.indexOf(',', p1+1);
        String rol = linea.substring(p1+1, p2);
        nuevo += linea.substring(0, p1) + "," + rol + "," + nuevoNombre + "\n";
        encontrado = true;
        continue;
      }
    }
    nuevo += linea + "\n";
  }
  f.close();
  if (encontrado) {
    SD.remove("/usuarios.txt");
    escribirArchivo("/usuarios.txt", nuevo, false);
  }
  return encontrado;
}

// ==================== REGISTRO UNIFICADO ====================
void registrarEvento(String tipo, String descripcion) {
  String linea = tipo + ":" + obtenerFechaHora() + " " + descripcion;
  escribirArchivo("/log.txt", linea);
  if (btConectado) SerialBT.println(linea);
}

// ==================== GESTIÓN DE COMPONENTES ====================
void cargarComponentesDesdeSD() {
  numComponentes = 0;
  File f = SD.open("/componentes.txt", FILE_READ);
  if (!f) return;
  while (f.available() && numComponentes < MAX_COMPONENTES) {
    String linea = leerLinea(f);
    int p1 = linea.indexOf(',');
    int p2 = linea.lastIndexOf(',');
    if (p1 > 0 && p2 > p1) {
      uidComponentes[numComponentes] = linea.substring(0, p1);
      nombreComponentes[numComponentes] = linea.substring(p1 + 1, p2);
      stockComponentes[numComponentes] = linea.substring(p2 + 1).toInt();
      idComponentes[numComponentes] = numComponentes + 1;
      numComponentes++;
    }
  }
  f.close();
}

void actualizarStockPorUID(String uidComp, int delta) {
  for (int i = 0; i < numComponentes; i++) {
    if (uidComponentes[i] == uidComp) {
      stockComponentes[i] += delta;
      break;
    }
  }
  SD.remove("/componentes.txt");
  for (int i = 0; i < numComponentes; i++) {
    escribirArchivo("/componentes.txt",
      uidComponentes[i] + "," + nombreComponentes[i] + "," + String(stockComponentes[i]));
  }
}

int buscarIndicePorUID(String uidBuscado) {
  String uidNorm = normalizarUID(uidBuscado);
  for (int i = 0; i < numComponentes; i++) {
    if (normalizarUID(uidComponentes[i]) == uidNorm) return i;
  }
  return -1;
}

// ==================== PRÉSTAMOS ====================
void registrarPrestamo(String uidUsuario, String uidComp, int cantidad) {
  String tiempo = obtenerFechaHora();
  escribirArchivo("/prestamos.txt", uidUsuario + "," + uidComp + "," + String(cantidad) + "," + tiempo);
}

int consultarCantidadPrestada(String uidUsuario, String uidComp) {
  File f = SD.open("/prestamos.txt", FILE_READ);
  if (!f) return 0;
  String uidUNorm = normalizarUID(uidUsuario);
  String uidCNorm = normalizarUID(uidComp);
  while (f.available()) {
    String linea = leerLinea(f);
    int p1 = linea.indexOf(',');
    int p2 = linea.indexOf(',', p1+1);
    int p3 = linea.lastIndexOf(',');
    if (normalizarUID(linea.substring(0, p1)) == uidUNorm && normalizarUID(linea.substring(p1+1, p2)) == uidCNorm) {
      f.close();
      return linea.substring(p2+1, p3).toInt();
    }
  }
  f.close();
  return 0;
}

void actualizarPrestamo(String uidUsuario, String uidComp, int nuevaCantidad) {
  File f = SD.open("/prestamos.txt", FILE_READ);
  if (!f) return;
  String nuevo = "";
  String uidUNorm = normalizarUID(uidUsuario);
  String uidCNorm = normalizarUID(uidComp);
  while (f.available()) {
    String linea = leerLinea(f);
    int p1 = linea.indexOf(',');
    int p2 = linea.indexOf(',', p1+1);
    int p3 = linea.lastIndexOf(',');
    if (normalizarUID(linea.substring(0, p1)) == uidUNorm && normalizarUID(linea.substring(p1+1, p2)) == uidCNorm) {
      if (nuevaCantidad > 0) {
        nuevo += linea.substring(0, p2+1) + String(nuevaCantidad) + "," + linea.substring(p3+1) + "\n";
      }
    } else {
      nuevo += linea + "\n";
    }
  }
  f.close();
  SD.remove("/prestamos.txt");
  escribirArchivo("/prestamos.txt", nuevo, false);
}

String obtenerDeudaUsuario(String uidUsuario) {
  String deuda = "";
  File f = SD.open("/prestamos.txt", FILE_READ);
  if (!f) return "";
  String uidUNorm = normalizarUID(uidUsuario);
  while (f.available()) {
    String linea = leerLinea(f);
    int p1 = linea.indexOf(',');
    int p2 = linea.indexOf(',', p1+1);
    int p3 = linea.lastIndexOf(',');
    if (normalizarUID(linea.substring(0, p1)) == uidUNorm) {
      String uidComp = linea.substring(p1+1, p2);
      int cant = linea.substring(p2+1, p3).toInt();
      String tiempo = linea.substring(p3+1);
      int idx = buscarIndicePorUID(uidComp);
      if (idx != -1) {
        deuda += nombreComponentes[idx] + "=" + String(cant) + "|" + tiempo + "\n";
      }
    }
  }
  f.close();
  return deuda;
}

bool usuarioTieneDeuda(String uidUsuario) {
  File f = SD.open("/prestamos.txt", FILE_READ);
  if (!f) return false;
  String uidUNorm = normalizarUID(uidUsuario);
  while (f.available()) {
    String linea = leerLinea(f);
    if (normalizarUID(linea.substring(0, linea.indexOf(','))) == uidUNorm) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

String obtenerDetallePrestamosUsuario(String uidUsuario) {
  String detalle = "";
  File f = SD.open("/prestamos.txt", FILE_READ);
  if (!f) return "";
  String uidUNorm = normalizarUID(uidUsuario);
  while (f.available()) {
    String linea = leerLinea(f);
    int p1 = linea.indexOf(',');
    int p2 = linea.indexOf(',', p1+1);
    int p3 = linea.lastIndexOf(',');
    if (normalizarUID(linea.substring(0, p1)) == uidUNorm) {
      String uidComp = linea.substring(p1+1, p2);
      int cant = linea.substring(p2+1, p3).toInt();
      int idx = buscarIndicePorUID(uidComp);
      if (idx != -1) {
        detalle += nombreComponentes[idx] + " x" + String(cant) + ", ";
      }
    }
  }
  f.close();
  if (detalle.length() > 2) detalle = detalle.substring(0, detalle.length() - 2);
  return detalle;
}

// ==================== ESCANEAR TARJETA CON CANCELACIÓN ====================
String esperarTarjeta(String mensaje1, String mensaje2, String mensaje3) {
  lcd.clear();
  lcd.setCursor(1, 0); lcd.print(mensaje1);
  lcd.setCursor(1, 1); lcd.print(mensaje2);
  lcd.setCursor(0, 3); lcd.print(mensaje3);
  ultimoTiempoActividad = millis();
  while (true) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uid = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) uid += "0";
        uid += String(rfid.uid.uidByte[i], HEX);
        if (i < rfid.uid.size - 1) uid += ":";
      }
      uid.toUpperCase();
      rfid.PICC_HaltA();
      return uid;
    }
    char tecla = obtenerTecla();
    if (tecla == '*' || tecla == 'T') {
      return "";
    }
  }
}

// ==================== MENÚ ESTUDIANTE (CON TIMEOUT) ====================
void menuEstudiante() {
  ultimoTiempoActividad = millis();
  while (true) {
    char op = mostrarMenu4("A:Solicitar", "B:Devolver", "C:Ver Deuda", "D:Salir");
    if (op == 'D' || op == 'T') break;

    if (op == 'A') {
      bool continuar = true;
      while (continuar) {
        String uidComp = esperarTarjeta("", "Escanee componente", "*:Cancelar");
        if (uidComp == "") { continuar = false; break; }
        int idx = buscarIndicePorUID(uidComp);
        if (idx == -1) {
          lcd.clear();
          lcd.setCursor(3, 1); lcd.print("Componente no");
          lcd.setCursor(4, 2); lcd.print("encontrado");
          delay(1500);
          continue;
        }
        char sub = mostrarMenu4(
          nombreComponentes[idx] + " (" + String(stockComponentes[idx]) + ")",
          "A:Ingresar Cantidad",
          "B:Re-escanear",
          "C:Cancelar");
        if (sub == 'C' || sub == '*' || sub == 'T') { continuar = false; break; }
        if (sub == 'B') continue;

        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(nombreComponentes[idx] + " (" + String(stockComponentes[idx]) + ")");
        lcd.setCursor(0, 1); lcd.print("Cantidad:          ");
        lcd.setCursor(0, 2); lcd.print("#:Aceptar *:Cancelar");
        lcd.setCursor(0, 3); lcd.print("C:Borrar");
        String cantStr = "";
        int posCursor = 10;
        ultimoTiempoActividad = millis();
        while (true) {
          char tecla = obtenerTecla();
          if (tecla >= '0' && tecla <= '9' && cantStr.length() < 2) {
            cantStr += tecla;
            lcd.setCursor(posCursor + cantStr.length() - 1, 1);
            lcd.print(tecla);
          } else if (tecla == 'C' && cantStr.length() > 0) {
            cantStr = cantStr.substring(0, cantStr.length() - 1);
            lcd.setCursor(posCursor + cantStr.length(), 1);
            lcd.print(' ');
            lcd.setCursor(posCursor + cantStr.length(), 1);
          } else if (tecla == '#' && cantStr.length() > 0) {
            int cant = cantStr.toInt();
            if (cant <= 0) {
              mostrarMensaje("Cantidad invalida", "Minimo 1", 1500);
              continuar = false;
            } else if (cant > stockComponentes[idx]) {
              mostrarMensaje("Stock insuficiente", "", 1500);
              continuar = false;
            } else {
              actualizarStockPorUID(uidComp, -cant);
              registrarPrestamo(usuarioActualUID, uidComp, cant);
              registrarEvento("PRESTAMO", usuarioActualNombre + " -> " + nombreComponentes[idx] + " x" + String(cant));
              lcd.clear(); lcd.setCursor(0, 0); lcd.print("Registrado!");
              delay(1000);
              char otro = mostrarMenu4("Deseas otro item?", "A:Si B:No", "", "");
              if (otro == 'A') continuar = true;
              else continuar = false;
            }
            break;
          } else if (tecla == '*' || tecla == 'T') { continuar = false; break; }
        }
      }
    }
    else if (op == 'B') {
      if (!usuarioTieneDeuda(usuarioActualUID)) {
        mostrarMensaje("Sin deuda pendiente", "", 2000);
        continue;
      }
      char sub = mostrarMenu4("A:Devolver todo", "B:Escanear comp.", "C:Cancelar", "");
      if (sub == 'A') {
        String detalle = obtenerDetallePrestamosUsuario(usuarioActualUID);
        File f = SD.open("/prestamos.txt", FILE_READ);
        if (f) {
          while (f.available()) {
            String linea = leerLinea(f);
            int p1 = linea.indexOf(',');
            int p2 = linea.indexOf(',', p1+1);
            int p3 = linea.lastIndexOf(',');
            if (normalizarUID(linea.substring(0, p1)) == normalizarUID(usuarioActualUID)) {
              String uidComp = linea.substring(p1+1, p2);
              int cant = linea.substring(p2+1, p3).toInt();
              actualizarStockPorUID(uidComp, cant);
            }
          }
          f.close();
          f = SD.open("/prestamos.txt", FILE_READ);
          String nuevo = "";
          while (f.available()) {
            String linea = leerLinea(f);
            if (!linea.startsWith(usuarioActualUID)) nuevo += linea + "\n";
          }
          f.close();
          SD.remove("/prestamos.txt");
          escribirArchivo("/prestamos.txt", nuevo, false);
          registrarEvento("DEVOLUCION_TOTAL", usuarioActualNombre + " [" + detalle + "]");
          mostrarMensaje("Devuelto todo", "Entregar al prof.", 2000);
        }
      } else if (sub == 'B') {
        String uidComp = esperarTarjeta("", "Escanee componente", "*:Cancelar");
        if (uidComp == "") continue;
        int idx = buscarIndicePorUID(uidComp);
        if (idx == -1) {
          lcd.clear();
          lcd.setCursor(3, 1); lcd.print("Componente no");
          lcd.setCursor(4, 2); lcd.print("encontrado");
          delay(1500);
          continue;
        }
        int prestado = consultarCantidadPrestada(usuarioActualUID, uidComp);
        if (prestado == 0) {
          mostrarMensaje("No tienes este", "componente", 1500);
          continue;
        }
        char devOp = mostrarMenu4(
          nombreComponentes[idx] + " (" + String(prestado) + ")",
          "A:Ingresar Cantidad",
          "B:Devolver todo",
          "C:Cancelar");
        if (devOp == 'C' || devOp == '*' || devOp == 'T') continue;
        if (devOp == 'B') {
          actualizarStockPorUID(uidComp, prestado);
          actualizarPrestamo(usuarioActualUID, uidComp, 0);
          registrarEvento("DEVOLUCION_TOTAL_COMP", usuarioActualNombre + " " + nombreComponentes[idx] + " x" + String(prestado));
          mostrarMensaje("Devuelto todo", "", 1500);
        } else if (devOp == 'A') {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print(nombreComponentes[idx] + " (" + String(prestado) + ")");
          lcd.setCursor(0, 1); lcd.print("Devolver:          ");
          lcd.setCursor(0, 2); lcd.print("#:Aceptar *:Cancelar");
          lcd.setCursor(0, 3); lcd.print("C:Borrar");
          String cantStr = "";
          int posCursor = 10;
          ultimoTiempoActividad = millis();
          while (true) {
            char tecla = obtenerTecla();
            if (tecla >= '0' && tecla <= '9' && cantStr.length() < 2) {
              cantStr += tecla;
              lcd.setCursor(posCursor + cantStr.length() - 1, 1);
              lcd.print(tecla);
            } else if (tecla == 'C' && cantStr.length() > 0) {
              cantStr = cantStr.substring(0, cantStr.length() - 1);
              lcd.setCursor(posCursor + cantStr.length(), 1);
              lcd.print(' ');
              lcd.setCursor(posCursor + cantStr.length(), 1);
            } else if (tecla == '#' && cantStr.length() > 0) {
              int cant = cantStr.toInt();
              if (cant <= 0) {
                mostrarMensaje("Cantidad invalida", "Minimo 1", 1500);
              } else if (cant > prestado) {
                mostrarMensaje("Cantidad excede", "prestamo", 1500);
              } else {
                actualizarStockPorUID(uidComp, cant);
                actualizarPrestamo(usuarioActualUID, uidComp, prestado - cant);
                registrarEvento("DEVOLUCION", usuarioActualNombre + " " + nombreComponentes[idx] + " x" + String(cant));
                mostrarMensaje("Devuelto", "", 1000);
              }
              break;
            } else if (tecla == '*' || tecla == 'T') break;
          }
        }
      }
    }
    else if (op == 'C') {
      String deuda = obtenerDeudaUsuario(usuarioActualUID);
      if (deuda.length() == 0) {
        mostrarMensaje("Sin deuda pendiente", "", 2000);
      } else {
        mostrarListaNavegable(deuda, "Deuda:");
      }
    }
  }
}

// ==================== MENÚ PROFESOR (CON TIMEOUT) ====================
void menuProfesor() {
  ultimoTiempoActividad = millis();
  while (true) {
    char op = mostrarMenu4("A:Anadir tarjeta", "B:Eliminar tarjeta", "C:Ver UID", "D:Salir");
    if (op == 'D' || op == 'T') break;

    if (op == 'A') {
      String uid = esperarTarjeta("", "Escanee nuevo UID", "*:Cancelar");
      if (uid == "") continue;
      if (buscarUsuario(uid) != "") {
        mostrarMensaje("Usuario ya existe", "", 2000);
        continue;
      }
      char rolOp = mostrarMenu4("Rol:", "A:Estudiante", "B:Profesor", "*:Cancelar");
      if (rolOp == 'A' || rolOp == 'B') {
        int rol = (rolOp == 'A') ? 2 : 1;

        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Ingrese cedula:");
        lcd.setCursor(0, 2); lcd.print("#:Aceptar *:Cancelar");
        lcd.setCursor(0, 3); lcd.print("C:Borrar");
        String cedula = "";
        int posCursor = 0;
        ultimoTiempoActividad = millis();

        while (true) {
          char tecla = obtenerTecla();
          if (tecla >= '0' && tecla <= '9' && cedula.length() < 12) {
            cedula += tecla;
            lcd.setCursor(posCursor + cedula.length() - 1, 1);
            lcd.print(tecla);
          } else if (tecla == 'C' && cedula.length() > 0) {
            cedula = cedula.substring(0, cedula.length() - 1);
            lcd.setCursor(posCursor + cedula.length(), 1);
            lcd.print(' ');
            lcd.setCursor(posCursor + cedula.length(), 1);
          } else if (tecla == '#' && cedula.length() > 0) {
            break;
          } else if (tecla == '*' || tecla == 'T') {
            cedula = "";
            break;
          }
        }

        if (cedula.length() > 0) {
          agregarUsuario(uid, cedula, rol);
          mostrarMensaje("Tarjeta agregada", cedula, 3000);
          registrarEvento("ALTA_USUARIO", cedula + " UID:" + uid);
        }
      }  
    }
    else if (op == 'B') {
      String uid = esperarTarjeta("Escanee tarjeta", "a eliminar", "*:Cancelar");
      if (uid == "") continue;
      String datos = buscarUsuario(uid);
      if (datos == "") {
        lcd.clear();
        lcd.setCursor(5, 1); lcd.print("Usuario no");
        lcd.setCursor(5, 2); lcd.print("encontrado");
        delay(2000);
        continue;
      }
      char conf = mostrarMenu4("Eliminar a:", datos.substring(2), "A:Si B:No", "");
      if (conf == 'A') {
        eliminarUsuario(uid);
        mostrarMensaje("Usuario eliminado", "", 2000);
        registrarEvento("BAJA_USUARIO", "UID:" + uid);
      }
    }
    else if (op == 'C') {
      String uid = esperarTarjeta("", "Escanee tarjeta", "*:Cancelar");
      if (uid == "") continue;
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("UID: " + uid);
      String datosUsuario = buscarUsuario(uid);
      if (datosUsuario != "") {
        lcd.setCursor(0,1); lcd.print("Nombre: " + datosUsuario.substring(2));
        lcd.setCursor(0,2); lcd.print("Rol: " + String(datosUsuario.charAt(0)=='1'?"Profesor":"Estudiante"));
      } else {
        int idx = buscarIndicePorUID(uid);
        if (idx != -1) {
          lcd.setCursor(0,1); lcd.print("Componente: " + nombreComponentes[idx]);
          lcd.setCursor(0,2); lcd.print("Stock: " + String(stockComponentes[idx]));
        } else {
          lcd.setCursor(0,1); lcd.print("No registrada");
        }
      }
      lcd.setCursor(0,3); lcd.print("OK para salir");
      while (obtenerTecla() == 0 && (millis() - ultimoTiempoActividad < 20000));
      delay(500);
    }
  }
}

// ==================== MODO COMANDOS ====================
const String PASSWORD = "admin123";
bool modoPC = false;
String bufferComando = "";

void procesarComando(String cmd) {
  cmd.trim();
  if (cmd.startsWith("AUT ")) {
    if (cmd.substring(4) == PASSWORD) {
      SerialBT.println("OK AUTENTICADO");
      modoPC = true;
      lcd.clear(); 
      lcd.setCursor(3,1); lcd.print("Modo Monitoreo");
      lcd.setCursor(0,2); lcd.print("Comandos habilitados");
      delay(2000);
      lcd.clear();
      lcd.setCursor(2,1); lcd.print("Escanee tarjeta");
      lcd.setCursor(0,3); lcd.print("BT Conectado");
    } else {
      SerialBT.println("ERROR Autenticacion fallida");
      SerialBT.disconnect();
    }
    return;
  }
  if (!modoPC) { SerialBT.println("ERROR No autenticado"); return; }

  auto getParams = [&](const String &prefix) {
    int idx = cmd.indexOf(' ');
    if (idx == -1) return String("");
    return cmd.substring(idx + 1);
  };

  if (cmd == "OBTENER_USUARIOS") {
    File f = SD.open("/usuarios.txt", FILE_READ);
    if (f) {
      enviarArchivoFiltrado(f);
      f.close();
    }
    SerialBT.println("\nFIN");
  }
  else if (cmd.startsWith("AGREGAR_USUARIO ")) {
    // (código sin cambios)
    String params = getParams("AGREGAR_USUARIO");
    int p1 = params.indexOf(' ');
    if (p1 > 0) {
      String uid = params.substring(0, p1);
      String rest = params.substring(p1 + 1);
      int p2 = rest.indexOf(' ');
      int rol;
      String nombre;
      if (p2 > 0) {
        String posibleRol = rest.substring(0, p2);
        int num = posibleRol.toInt();
        if (num == 1 || num == 2) {
          rol = num;
          nombre = rest.substring(p2 + 1);
        } else {
          rol = 2;
          nombre = rest;
        }
      } else {
        rol = 2;
        nombre = rest;
      }
      if (buscarUsuario(uid) != "")
        SerialBT.println("ERROR Ya existe");
      else {
        agregarUsuario(uid, nombre, rol);
        registrarEvento("ALTA_USUARIO", nombre + " UID:" + uid);
        SerialBT.println("OK Usuario agregado");
      }
    } else {
      SerialBT.println("ERROR Formato: UID ROL NOMBRE o UID NOMBRE");
    }
  }
  else if (cmd.startsWith("ELIMINAR_USUARIO ")) {
    // (código sin cambios)
    String uid = getParams("ELIMINAR_USUARIO");
    bool ok = eliminarUsuario(uid);
    if (ok) {
      registrarEvento("BAJA_USUARIO", "UID:" + uid);
      SerialBT.println("OK Usuario eliminado");
    } else {
      SerialBT.println("ERROR No encontrado");
    }
  }
  else if (cmd.startsWith("CAMBIAR_NOMBRE ")) {
    // (código sin cambios)
    String params = getParams("CAMBIAR_NOMBRE");
    int p = params.indexOf(' ');
    if (p > 0) {
      String uid = params.substring(0, p);
      String nuevoNombre = params.substring(p + 1);
      if (modificarNombreUsuario(uid, nuevoNombre)) {
        registrarEvento("CAMBIO_NOMBRE", "UID:" + uid + " -> " + nuevoNombre);
        SerialBT.println("OK Nombre actualizado");
      } else {
        SerialBT.println("ERROR Usuario no encontrado");
      }
    } else {
      SerialBT.println("ERROR Formato: CAMBIAR_NOMBRE UID NUEVO_NOMBRE");
    }
  }
  else if (cmd == "OBTENER_COMPONENTES") {
    File f = SD.open("/componentes.txt", FILE_READ);
    if (f) {
      enviarArchivoFiltrado(f);
      f.close();
    }
    SerialBT.println("\nFIN");
  }
  else if (cmd.startsWith("AGREGAR_COMPONENTE ")) {
    // (código sin cambios)
    String params = getParams("AGREGAR_COMPONENTE");
    int p1 = params.indexOf(' ');
    int p2 = params.lastIndexOf(' ');
    if (p1 > 0 && p2 > p1) {
      String tag = params.substring(0, p1);
      String nombre = params.substring(p1 + 1, p2);
      int stock = params.substring(p2 + 1).toInt();
      int idx = buscarIndicePorUID(tag);
      if (idx == -1) {
        if (numComponentes < MAX_COMPONENTES) {
          uidComponentes[numComponentes] = tag;
          nombreComponentes[numComponentes] = nombre;
          stockComponentes[numComponentes] = stock;
          idComponentes[numComponentes] = numComponentes + 1;
          numComponentes++;
        } else SerialBT.println("ERROR Max componentes");
      } else {
        nombreComponentes[idx] = nombre;
        stockComponentes[idx] = stock;
      }
      SD.remove("/componentes.txt");
      for (int i = 0; i < numComponentes; i++)
        escribirArchivo("/componentes.txt", uidComponentes[i] + "," + nombreComponentes[i] + "," + String(stockComponentes[i]));
      registrarEvento("COMPONENTE_MOD", tag + " " + nombre + " stock:" + String(stock));
      SerialBT.println("OK Componente actualizado");
    } else {
      SerialBT.println("ERROR Formato: AGREGAR_COMPONENTE TAG NOMBRE STOCK");
    }
  }
  else if (cmd.startsWith("ELIMINAR_COMPONENTE ")) {
    // (código sin cambios)
    String tag = getParams("ELIMINAR_COMPONENTE");
    int idx = buscarIndicePorUID(tag);
    if (idx != -1) {
      String nombre = nombreComponentes[idx];
      for (int i = idx; i < numComponentes - 1; i++) {
        uidComponentes[i] = uidComponentes[i + 1];
        nombreComponentes[i] = nombreComponentes[i + 1];
        stockComponentes[i] = stockComponentes[i + 1];
        idComponentes[i] = idComponentes[i + 1];
      }
      numComponentes--;
      SD.remove("/componentes.txt");
      for (int i = 0; i < numComponentes; i++)
        escribirArchivo("/componentes.txt", uidComponentes[i] + "," + nombreComponentes[i] + "," + String(stockComponentes[i]));
      registrarEvento("COMPONENTE_ELIM", tag + " " + nombre);
      SerialBT.println("OK Componente eliminado");
    } else {
      SerialBT.println("ERROR No encontrado");
    }
  }
  else if (cmd.startsWith("MODIFICAR_STOCK ")) {
    // (código sin cambios)
    String params = getParams("MODIFICAR_STOCK");
    int p = params.indexOf(' ');
    if (p > 0) {
      String tag = params.substring(0, p);
      int cant = params.substring(p + 1).toInt();
      int idx = buscarIndicePorUID(tag);
      if (idx != -1) {
        stockComponentes[idx] = cant;
        SD.remove("/componentes.txt");
        for (int i = 0; i < numComponentes; i++)
          escribirArchivo("/componentes.txt", uidComponentes[i] + "," + nombreComponentes[i] + "," + String(stockComponentes[i]));
        registrarEvento("STOCK_MOD", tag + " " + nombreComponentes[idx] + " nuevo stock:" + String(cant));
        SerialBT.println("OK Stock actualizado");
      } else {
        SerialBT.println("ERROR No encontrado");
      }
    } else {
      SerialBT.println("ERROR Formato: MODIFICAR_STOCK TAG CANTIDAD");
    }
  }
  else if (cmd == "OBTENER_PRESTAMOS") {
    File f = SD.open("/prestamos.txt", FILE_READ);
    if (f) {
      char bufSalida[512];
      int posSalida = 0;
      char bufLectura[256];
      int posLectura = 0, lenLectura = 0;

      while (true) {
        String linea = "";
        while (true) {
          if (posLectura >= lenLectura) {
            lenLectura = f.read((uint8_t*)bufLectura, sizeof(bufLectura));
            posLectura = 0;
            if (lenLectura == 0) break;
          }
          char c = bufLectura[posLectura++];
          if (c == '\n') break;
          linea += c;
        }
        if (linea.length() == 0 && lenLectura == 0) break;
        linea.trim();
        if (linea.length() == 0) continue;

        int p1 = linea.indexOf(',');
        int p2 = linea.indexOf(',', p1 + 1);
        int p3 = linea.lastIndexOf(',');
        if (p1 > 0 && p2 > p1 && p3 > p2) {
          String uidUsuario = linea.substring(0, p1);
          String uidComp = linea.substring(p1 + 1, p2);
          String cantidad = linea.substring(p2 + 1, p3);
          String timestamp = linea.substring(p3 + 1);

          String nombreUsuario = uidUsuario;
          String datosUser = buscarUsuario(uidUsuario);
          if (datosUser != "") {
            int posComa = datosUser.indexOf(',');
            if (posComa > 0) nombreUsuario = datosUser.substring(posComa + 1);
            else nombreUsuario = datosUser;
          }
          String nombreComp = uidComp;
          int idx = buscarIndicePorUID(uidComp);
          if (idx != -1) nombreComp = nombreComponentes[idx];

          String lineaSalida = nombreUsuario + "|" + nombreComp + "|" + cantidad + "|" + timestamp + "\n";

          for (int i = 0; i < lineaSalida.length(); i++) {
            bufSalida[posSalida++] = lineaSalida[i];
            if (posSalida >= sizeof(bufSalida) - 1) {
              SerialBT.write((uint8_t*)bufSalida, posSalida);
              posSalida = 0;
            }
          }
        }
      }
      if (posSalida > 0) SerialBT.write((uint8_t*)bufSalida, posSalida);
      f.close();
    }
    SerialBT.println("\nFIN");
  }
  else if (cmd == "OBTENER_HISTORIAL") {
    const int MAX_LINEAS = 500;
    File f = SD.open("/log.txt", FILE_READ);
    if (f) {
      // ----- PRIMERA PASADA: contar líneas totales -----
      int totalLineas = 0;
      char buf[128];
      int len;
      while ((len = f.read((uint8_t*)buf, sizeof(buf))) > 0) {
        for (int i = 0; i < len; i++) {
          if (buf[i] == '\n') totalLineas++;
        }
      }
      f.close();

      // ----- SEGUNDA PASADA: enviar últimas MAX_LINEAS líneas -----
      f = SD.open("/log.txt", FILE_READ);
      if (f) {
        int lineaInicio = (totalLineas > MAX_LINEAS) ? totalLineas - MAX_LINEAS : 0;
        int lineaActual = 0;          // línea actual (0 = primera línea)
        char salida[512];
        int posSalida = 0;
        bool nuevaLinea = true;
        bool enviar = (lineaInicio == 0);

        while ((len = f.read((uint8_t*)buf, sizeof(buf))) > 0) {
          for (int i = 0; i < len; i++) {
            char c = buf[i];
            if (c == '\n') {
              // Fin de línea
              if (!nuevaLinea) {
                if (enviar) {
                  salida[posSalida++] = '\n';
                  if (posSalida >= sizeof(salida) - 1) {
                    SerialBT.write((uint8_t*)salida, posSalida);
                    posSalida = 0;
                  }
                }
                nuevaLinea = true;
              }
              lineaActual++;
              if (lineaActual >= lineaInicio) {
                enviar = true;   // comenzar a enviar a partir de la siguiente línea
              }
            } else {
              // Carácter normal
              if (nuevaLinea) {
                nuevaLinea = false;
              }
              if (enviar) {
                if (c == ',') c = '|';
                salida[posSalida++] = c;
                if (posSalida >= sizeof(salida) - 1) {
                  SerialBT.write((uint8_t*)salida, posSalida);
                  posSalida = 0;
                }
              }
            }
          }
        }
        if (posSalida > 0) {
          SerialBT.write((uint8_t*)salida, posSalida);
        }
        f.close();
      }
    }
    SerialBT.println("\nFIN");
  }

  else if (cmd == "HISTORIAL_HOY") {
    String hoy = obtenerFechaHoy();
    File f = SD.open("/log.txt", FILE_READ);
    if (f) {
      char bufSalida[512];
      int posSalida = 0;
      char bufLectura[256];
      int posLectura = 0, lenLectura = 0;

      while (true) {
        String linea = ""; 
        while (true) {
          if (posLectura >= lenLectura) {
            lenLectura = f.read((uint8_t*)bufLectura, sizeof(bufLectura));
            posLectura = 0;
            if (lenLectura == 0) break;
          }
          char c = bufLectura[posLectura++];
          if (c == '\n') break;
          linea += c;
        }
        if (linea.length() == 0 && lenLectura == 0) break;
        linea.trim();
        if (linea.length() == 0) continue;

        int posDosPuntos = linea.indexOf(':');
        if (posDosPuntos != -1) {
          String fechaLinea = linea.substring(posDosPuntos + 1, posDosPuntos + 11);
          if (fechaLinea == hoy) {
            linea.replace(",", " | ");
            linea += "\n";
            for (int i = 0; i < linea.length(); i++) {
              bufSalida[posSalida++] = linea[i];
              if (posSalida >= sizeof(bufSalida) - 1) {
                SerialBT.write((uint8_t*)bufSalida, posSalida);
                posSalida = 0;
              }
            }
          }
        }
      }
      if (posSalida > 0) SerialBT.write((uint8_t*)bufSalida, posSalida);
      f.close();
    }
    SerialBT.println("\nFIN");
  }
  else if (cmd.startsWith("BUSCAR_DEUDA ")) {
    String uid = getParams("BUSCAR_DEUDA");
    String deuda = obtenerDeudaUsuario(uid);
    if (deuda.length() == 0) SerialBT.println("Sin deuda");
    else {
      SerialBT.print(deuda);
      SerialBT.println("FIN");
    }
  }
  else if (cmd.startsWith("BUSCAR_DEUDA_NOMBRE ")) {
    String nombre = getParams("BUSCAR_DEUDA_NOMBRE");
    File f = SD.open("/usuarios.txt", FILE_READ);
    String uid = "";
    if (f) {
      while (f.available()) {
        String linea = leerLinea(f);
        int p = linea.lastIndexOf(',');
        if (p > 0 && linea.substring(p + 1) == nombre) {
          uid = linea.substring(0, linea.indexOf(','));
          break;
        }
      }
      f.close();
    }
    if (uid != "") {
      String deuda = obtenerDeudaUsuario(uid);
      if (deuda.length() == 0) SerialBT.println("Sin deuda");
      else {
        SerialBT.print(deuda);
        SerialBT.println("FIN");
      }
    } else {
      SerialBT.println("ERROR Usuario no encontrado");
    }
  }
  else if (cmd.startsWith("SET_TIME ")) {
    // (código sin cambios)
    String params = getParams("SET_TIME");
    int y, mo, d, h, mi, s;
    if (sscanf(params.c_str(), "%d %d %d %d %d %d", &y, &mo, &d, &h, &mi, &s) == 6) {
      DateTime dt = {y, mo, d, h, mi, s};
      DS1302_setTime(dt);
      registrarEvento("HORA_AJUSTADA", "Nueva fecha/hora");
      SerialBT.println("OK Hora actualizada");
    } else {
      SerialBT.println("ERROR Formato. Use: SET_TIME AAAA MM DD HH MM SS");
    }
  }
  else if (cmd == "SALIR") {
    modoPC = false;
    SerialBT.println("OK Sesion cerrada");
    lcd.clear(); lcd.setCursor(1,1); lcd.print("Acerca una tarjeta");
  }
  else {
    SerialBT.println("ERROR Comando desconocido");
  }
}

// ==================== MOSTRAR PANTALLA INICIO ====================
void mostrarPantallaInicio() {
  lcd.clear();
  if (btConectado) {
    lcd.setCursor(0,3); lcd.print("BT Conectado");
    lcd.setCursor(1,1); lcd.print("Acerca una tarjeta");
  } else {
    lcd.setCursor(1,1); lcd.print("Acerca una tarjeta");
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  lcd.begin(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Sistema Inventario");
  lcd.setCursor(0, 1); lcd.print("Configurando...");

  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();

  SPI_HSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  lcd.setCursor(0, 2); lcd.print("SD...");
  if (!SD.begin(SD_CS, SPI_HSPI)) {
    lcd.setCursor(0, 3); lcd.print("Error SD");
    while (1);
  }
  lcd.setCursor(0, 3); lcd.print("SD OK");
  delay(1000);

  DS1302_init();
  delay(500);

  if (!SD.exists("/usuarios.txt")) {
    escribirArchivo("/usuarios.txt", "", false);
    primeraTarjeta = true;
  } else {
    File f = SD.open("/usuarios.txt", FILE_READ);
    if (f && f.size() == 0) primeraTarjeta = true;
    if (f) f.close();
  }
  if (!SD.exists("/componentes.txt")) {
    escribirArchivo("/componentes.txt", "", false);
  }
  if (!SD.exists("/prestamos.txt")) {
    escribirArchivo("/prestamos.txt", "", false);
  }

  cargarComponentesDesdeSD();

  SerialBT.begin("Sistema_Inventario");
  mostrarPantallaInicio();
}

// ==================== LOOP ====================
void loop() {
  // --- Gestión Bluetooth ---
  if (SerialBT.hasClient()) {
    if (!btConectado) {
      btConectado = true;
      lcd.clear(); 
      lcd.setCursor(0,3); lcd.print("BT Conectado");
      delay(1000);
      mostrarPantallaInicio();
      modoPC = false;
      bufferComando = "";
    }
    while (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '\n') {
        procesarComando(bufferComando);
        bufferComando = "";
      } else {
        bufferComando += c;
      }
    }
  } else {
    if (btConectado) {
      btConectado = false;
      modoPC = false;
      lcd.clear(); lcd.print("BT Desconectado");
      delay(1000);
      mostrarPantallaInicio();
    }
  }

  // --- RFID y menús locales ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(rfid.uid.uidByte[i], HEX);
      if (i < rfid.uid.size - 1) uid += ":";
    }
    uid.toUpperCase();
    rfid.PICC_HaltA();

    if (primeraTarjeta) {
      agregarUsuario(uid, "Profesor", 1);
      primeraTarjeta = false;
      registrarEvento("ALTA_USUARIO", "Profesor (inicial) UID:" + uid);
      lcd.clear(); lcd.print("Primera tarjeta"); lcd.setCursor(0, 1); lcd.print("Asignada Profesor");
      delay(1500);
      usuarioActualUID = uid; usuarioActualRol = 1; usuarioActualNombre = "Profesor";
      menuProfesor();
      usuarioActualUID = "";
      mostrarPantallaInicio();
    } else {
      String datos = buscarUsuario(uid);
      if (datos == "") {
        lcd.clear();
        lcd.setCursor(5, 1); lcd.print("Usuario no");
        lcd.setCursor(5, 2); lcd.print("encontrado");
        delay(2000);
        mostrarPantallaInicio();
      } else {
        usuarioActualUID = uid;
        usuarioActualRol = datos.charAt(0) - '0';
        usuarioActualNombre = datos.substring(2);
        if (usuarioActualRol == 1) menuProfesor();
        else menuEstudiante();
        usuarioActualUID = "";
        mostrarPantallaInicio();
      }
    }
  }

  // --- Reinicio automático cada 3 días a las 03:00 AM ---
  DateTime ahora = DS1302_getTime();
  if (ahora.hour == 3 && ahora.minute == 0 && ahora.second < 5) {
    if (ahora.day % 3 == 0) { 
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Reinicio programado");
      delay(3000);
      ESP.restart();
    }
  }
}