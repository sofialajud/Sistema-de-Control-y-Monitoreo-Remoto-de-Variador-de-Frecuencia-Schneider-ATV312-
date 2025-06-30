/*---------------------------------------------------------------

 *  Proyecto: Control y monitoreo de variador de frecuencia ATV312
 * 
 *  Descripción:
 *  Este código permite comandar (marcha/parada y referencia de velocidad)
 *  y monitorear (frecuencia y corriente de salida) un variador de frecuencia
 *  Schneider Electric ATV312, utilizando comunicación Modbus RTU a través
 *  de un Arduino Mega 2560.
 *  
 *  La interfaz gráfica se implementa mediante la plataforma Blynk,
 *  permitiendo el control y visualización remota desde una aplicación móvil.
 *  La conexión a internet se realiza mediante un módulo Ethernet ENC28J60.
 *
 *  Materia: Accionamientos Eléctricos y Electrónica Industrial
 *  Alumna: Lajud de la Puente, Sofía Micaela y Guerrero Dacia
 *  Fecha: [26/06/2025
 *  Docente: INGENIERO PONCE
 *
 *  Requisitos:
 *  - Arduino Mega 2560
 *  - Módulo RS485 para Modbus RTU
 *  - Variador de frecuencia ATV312 configurado en modo Modbus
 *  - Módulo Ethernet ENC28J60
 *  - Plataforma Blynk (con conexión activa)
 !!!
 Importante!!!: antes de compilar copie las carpetas que se 
 encuentra en el mismo repositorio en la carpeta de librerias
 -----------------------------------------------------*/ 
#define BLYNK_TEMPLATE_ID "TMPL2uBdyWKvE" 
#define BLYNK_TEMPLATE_NAME "Quickstart Template"  
#include <SPI.h>
#include <UIPEthernet.h>               // Para ENC28J60
#include <BlynkSimpleEthernetENC.h>    // Para ENC28J60
#include "Modbus.h"
#define BLYNK_PRINT Serial    // Monitor Blynk. Si no es necesario, comentar o eliminar

WidgetLED LEDSTS(V4);         // Clase WidgetLED, para control del LED/V4
WidgetLCD lcd(V5);            // Clase LCD de Blynk

uint8_t LED_PIN = 13;         // LED de estado

char auth[] = "Y7_hx-pOpZPqnv3x3AGUrS2QUwWorujY";    // Token de autenticación de la app Blynk.

uint16_t comando_ATV;               // 1 - comando de marcha, 0 - comando de paro
uint16_t frecuencia_seteada = 100;  // Referencia de frecuencia
uint16_t frecuencia_salida;      
uint16_t corriente_salida;        
uint8_t estado_ATV;


/*----------------------------------------------------------------- 
   Función llamada cada vez que se presiona el botón en la app 
-----------------------------------------------------------------*/
BLYNK_WRITE(V0)
{
   comando_ATV = param.asInt() & 0xFF;          // Carga el valor de la variable virtual V0

   if(comando_ATV == 1) comando_ATV = 0x000F;   // Convierte el comando para que el ATV312 lo entienda
   else comando_ATV = 0x100F;
   
   EnviaComando(comando_ATV, frecuencia_seteada); // Envía comando y referencia de frecuencia al ATV312
}

/*----------------------------------------------------------------- 
   Función llamada cada vez que se modifica el slider - V2
-----------------------------------------------------------------*/
BLYNK_WRITE(V2)
{
   frecuencia_seteada = (uint16_t)(param.asFloat()*10);   // Guarda V2 (frecuencia seteada)
   
   EnviaComando(comando_ATV, frecuencia_seteada);         // Envía comando y referencia de frecuencia al ATV312
}

/*----------------------------------------------------------------
    Envía al servidor los valores de frecuencia y corriente 
    de salida en las variables V1 y V3 respectivamente
-----------------------------------------------------------------*/
void EnviaDatos(void)
{
   Blynk.virtualWrite(V1, (float)frecuencia_salida/10);    
   Blynk.virtualWrite(V3, (float)corriente_salida/10);  
}

/*----------------------------------------------------------------
    Formatea variables para enviarlas al LCD de Blynk
-----------------------------------------------------------------*/
void PrintDatos(void)
{
   char buff[10];       // Variables auxiliares 
   uint8_t aux, cnt;    // para formateo de los datos
   
   aux = sprintf(buff, "%d%s", frecuencia_salida, "Hz  "); // Imprime en el buffer
  
   if(aux < 6)
   {
      for(cnt=7; cnt>1; cnt--) buff[cnt] = buff[cnt-2];  // Desplaza dos posiciones a la derecha
      buff[1] = '.';                                     // Inserta el "0." en el formato
      buff[0] = '0';
   }else
   {
      for(cnt=aux+1; cnt>(aux-6); cnt--) buff[cnt] = buff[cnt-1]; // Desplaza dos posiciones a la derecha
      buff[cnt+1] = '.';                                          // Inserta sólo el "."
   }
  
   lcd.print(0, 1, buff);  // Envía al display
  
   aux = sprintf(buff, "%d%s", corriente_salida,  "A  ");  // Imprime en el buffer
   if(aux < 5)
   {
      for(cnt=6; cnt>1; cnt--) buff[cnt] = buff[cnt-2];  // Desplaza dos posiciones a la derecha
      buff[1] = '.';                                     // Inserta el "0." en el formato
      buff[0] = '0';
   }else
   {
      for(cnt=aux+1; cnt>(aux-5); cnt--) buff[cnt] = buff[cnt-1]; // Desplaza dos posiciones a la derecha
      buff[cnt+1] = '.';                                          // Inserta sólo el "."
   }
   lcd.print(7, 1, buff);  // Envía al display
}

/*----------------------------------------------------------------
    Configuración inicial
-----------------------------------------------------------------*/
void setup(void)
{
   Serial.begin(115200);            // Inicia el monitor serial
   StartModbus();                   // Inicia el puerto e interfaz  
   
   Blynk.begin(auth);               // Autentica en el servidor
   Serial.println("¡Conectado!");
   
   pinMode(LED_PIN, OUTPUT);        // Configura el pin como salida
   digitalWrite(LED_PIN, 1);        // Coloca el pin en nivel alto

   // Los siguientes comandos son para inicializar el variador y deben enviarse después de energizarlo.
   EnviaComando(0x06, 0);            
   delay(250);
   EnviaComando(0x07, 0);            
   delay(250);
   EnviaComando(0x100F, 0); 
   
   lcd.clear();   // Limpia el LCD Widget
}

/*----------------------------------------------------------------
    Bucle principal
-----------------------------------------------------------------*/
void loop()
{
   static uint8_t bkled;   // Auxiliar para hacer parpadear el LED del Widget
   
   delay(500);    // Retardo entre lecturas
   
   Blynk.run();   // Magia. No tocar
   
   digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Invierte estado del pin, como indicador de estado
  
   if(LeRegistradores() == 0)    // Lee los registros del ATV312
   {    
      if((bkled++)%2) LEDSTS.on();   // Hace parpadear el LED en el proyecto Blynk
      else LEDSTS.off(); 

      estado_ATV = au16data[0] & 0xEF;    // Guarda estado
      frecuencia_salida = au16data[1];    // frecuencia 
      corriente_salida = au16data[3];     // y corriente de salida del ATV312  

      if(estado_ATV == 0x27)
      {   
         lcd.print(0, 0, "UNIDAD OK      ");
         PrintDatos();         
      }else
      {
         lcd.print(0, 0, "UNIDAD FALLA"); // Muestra mensaje en el LCD
         lcd.print(0, 1, "VERIFIQUE HMI");       
      }
   }else
   {
      LEDSTS.off();               // Si hay error en la lectura, apaga el LED en la app y reinicia variables
      frecuencia_salida = 0;
      corriente_salida = 0;  
      lcd.print(0, 0, "UNIDAD EN FALLA");  // Muestra mensaje en el LCD
      lcd.print(0, 1, "SIN CONEXIÓN   ");  // Sin conexión con el variador
   }
  
   EnviaDatos();  // Envía datos al servidor Blynk
}
