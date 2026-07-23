# Sistema de Inventario con ESP32 y RFID

Sistema embebido para la gestión de inventario en laboratorios educativos. Utiliza un ESP32, lector RFID RC522, pantalla LCD 20×4, teclado matricial 4x4, reloj RTC DS1302, almacenamiento en tarjeta SD y comunicación Bluetooth con una app Android.

## Características

- **Dos roles de usuario**: Profesor (administración) y Estudiante (préstamos y devoluciones).
- **Interfaz local** con pantalla LCD y teclado matricial 4×4.
- **Control de stock** en tiempo real con archivos en tarjeta SD.
- **Registro de transacciones** con fecha y hora (RTC DS1302).
- **App Android** para administración remota vía Bluetooth.
- **Autenticación por contraseña** en la app.
- **Reinicio automático programado** para máxima estabilidad.
- **Timeout de inactividad** en menús para evitar bloqueos.

## Materiales necesarios

| Componente | Descripción |
|------------|-------------|
| ESP32 DevKit V1 | Microcontrolador principal |
| RFID RC522 | Lector de tarjetas para usuarios y componentes |
| Pantalla LCD 20×4 | Interfaz visual |
| Teclado matricial 4×4 | Navegación y entrada de datos |
| RTC DS1302 | Reloj en tiempo real con batería de respaldo |
| Lector de tarjeta SD | Almacenamiento de datos |
| Módulo Bluetooth | Integrado en el ESP32 |
| Resistencias 10 kΩ | Pull‑up para columnas del teclado |

## Estructura del repositorio
├── SistemaDeInventario.ino # Firmware del ESP32
├── GestorInventarioESP32.apk # App Android (MIT App Inventor)
└── README.md # Este archivo


## Instalación del firmware

1. **Clona o descarga este repositorio** en tu PC.
2. **Abre el archivo `.ino` con Arduino IDE.
3. **Instala las librerías necesarias** (todas disponibles en el gestor de librerías):
   - `LiquidCrystal_I2C`
   - `MFRC522`
   - `Keypad`
   - `SD` (incluida en el core de ESP32)
   - `BluetoothSerial` (incluida)
4. **Conecta el hardware** según el pinout descrito en el código (sección `CONFIGURACIÓN DE PINES`).
5. **Compila y sube** el firmware a tu ESP32.
6. **Inserta una tarjeta microSD** formateada en FAT32.
7. **La primera tarjeta RFID que leas** se convertirá automáticamente en **Profesor**.

## Instalación de la app Android

1. Transfiere el archivo `GestorInventarioESP32.apk` a tu dispositivo Android.
2. Habilita "Orígenes desconocidos" en los ajustes de seguridad.
3. Instala la aplicación y ábrela.
4. Conéctate al ESP32 seleccionando "Sistema_Inventario" en la lista de dispositivos Bluetooth.
5. Autentícate con la contraseña (por defecto `admin123`).

## Uso del sistema

### Interfaz local (LCD + teclado)

- **Tecla A, B, C**: seleccionar opciones de menú.
- **Tecla D**: salir al menú anterior / cancelar.
- **Tecla ***: cancelar operación actual (escaneo, ingreso de datos).
- **Tecla #**: confirmar / aceptar.
- **Timeout de 20 segundos**: si no se pulsa ninguna tecla, el sistema vuelve a la pantalla de inicio.

### App Android

- **Usuarios**: censados, añadir, eliminar, cambiar nombre, buscar deudas (por UID o etiqueta asignada).
- **Componentes**: ver stock, añadir, eliminar, modificar stock.
- **Préstamos**: ver préstamos activos con identificador legibles.
- **Historial**: consultar últimos 500 movimientos o solo los del día.
- **Ajustar hora**: sincronizar el RTC del ESP32.

## Comandos Bluetooth (para desarrolladores)

La app se comunica mediante comandos de texto. Puedes probarlos con cualquier terminal Bluetooth:

| Comando | Descripción |
|---------|-------------|
| `AUT admin123` | Autenticación |
| `OBTENER_USUARIOS` | Lista de usuarios |
| `AGREGAR_USUARIO UID ROL NOMBRE` | Añadir usuario |
| `ELIMINAR_USUARIO UID` | Eliminar usuario |
| `CAMBIAR_NOMBRE UID NUEVO_NOMBRE` | Renombrar usuario |
| `OBTENER_COMPONENTES` | Lista de componentes |
| `AGREGAR_COMPONENTE TAG NOMBRE STOCK` | Añadir componente |
| `ELIMINAR_COMPONENTE TAG` | Eliminar componente |
| `MODIFICAR_STOCK TAG CANTIDAD` | Cambiar stock |
| `OBTENER_PRESTAMOS` | Préstamos activos |
| `OBTENER_HISTORIAL` | Últimos 500 movimientos |
| `HISTORIAL_HOY` | Movimientos del día |
| `BUSCAR_DEUDA UID` | Deuda por tarjeta |
| `BUSCAR_DEUDA_NOMBRE NOMBRE` | Deuda por nombre |
| `SET_TIME AAAA MM DD HH MM SS` | Ajustar RTC |
| `SALIR` | Cerrar sesión |

## Decisiones de diseño destacadas

- **Normalización de UID**: evita errores por diferencias de formato entre lectores RFID.
- **Reescritura atómica de archivos**: protección contra corrupción en cortes eléctricos.
- **Timeout en todos los menús**: impide que el sistema quede bloqueado.
- **Reinicio automático cada 3 días**: mantiene la memoria limpia de fragmentación.

## Licencia

Este proyecto se comparte con fines educativos. Siéntete libre de usarlo, modificarlo y adaptarlo a tus necesidades.
