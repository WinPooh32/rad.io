#include <stdio.h> // printf()
#include <stdint.h> // data types uint...
#include <memory.h> // memcpy()

#include <asf.h>
#include <stdio_serial.h>

#define PACKED __attribute__ ((__packed__))

// алиас для массива uint8_t фиксированной длины;
// 1 байт зарезервирован для кода команды;
// представляет данные командного пакета;
#define PACKET_MAX_SIZE 255
typedef uint8_t args_buf_t[PACKET_MAX_SIZE-1];

#define COMMAND_GET_DATA  0
#define COMMAND_SET_TIMER 1

#define BOOL_FALSE 0
#define BOOL_TRUE  1

/* Structures for SPI master, slave configuration & slave instance selection */
struct spi_module master;
struct spi_module slave;
struct spi_slave_inst slave_inst;

//--------------------------------------
//---------- Пакет с данными -----------
//--------------------------------------

// (4*3) + (1 * 83) + 4 + 4 + 4 = 83 + 4*6 = 107 байт
struct data_packet_t {
  uint32_t  id[3];
  uint8_t   gps_data[83];
  uint32_t  radiation;
  float     bat_voltage;
  float     detector_voltage;
} PACKED static packet;

const static size_t DATA_PACKET_SIZE = sizeof(packet);
//--------------------------------------


//--------------------------------------
//-------------- Команды ---------------
//--------------------------------------

//command_packet_t - пакет команды от станции
// 1 + 254 = 255 байт
struct command_packet_t {
  uint8_t command;
  args_buf_t raw_args;
} PACKED static command_packet;

// 1 байт
struct args_get_data_t {
    uint8_t update_gps;
} PACKED;

// extract_args_get_data преобразует массив байтов в структуру args_get_data_t
struct args_get_data_t extract_args_get_data(args_buf_t* buf){
    struct args_get_data_t args;
    memcpy(&args, buf, sizeof(args));
    return args;
}

struct args_set_timer_t {
    uint32_t secs;
} PACKED;

// extract_args_set_timer_t преобразует массив байтов в структуру args_set_timer_t
struct args_set_timer_t extract_args_set_timer_t(args_buf_t* buf){
    struct args_set_timer_t args;
    memcpy(&args, buf, sizeof(args));
    return args;
}
//--------------------------------------


//--------------------------------------
//-------- Таймер пробуждения ----------
//--------------------------------------
// FIXME подставить правильную констату
static const size_t SECOND = 9999;
static size_t timer = 0; // cpu ticks

// set_timer выставляет временной промежуток вещания в секундах
void set_timer(size_t secs){
    timer = secs * SECOND;
}
//--------------------------------------


//--------------------------------------
//------ Операции ввода-вывода ---------
//--------------------------------------

// cast_to_bytes преобразует структуру типа data_packet_t в массив
uint8_t* cast_to_bytes(struct data_packet_t* ptr){
    return (uint8_t*)ptr;
}

// copy_to_packet копирует из массива data в пакет по указателю ptr
void copy_to_packet(const void* data, struct data_packet_t* ptr){
    memcpy(ptr, data, sizeof(*ptr));
}

void send_to_radio(const uint8_t* data, const size_t length){
    /* Send data to slave */
    spi_select_slave(&master, &slave_inst, true);

    for(size_t i = 0; i < length; i++){
        spi_write(&master, *(data + i));
        while (!spi_is_write_complete(&master)) {}
    }
}

void recieve_from_radio(const uint8_t* data, const size_t length){
    spi_select_slave(&master, &slave_inst, true);

    while (!spi_is_ready_to_read(&master)) {
    }
    spi_read(&master, &rxd_data);

    for(size_t i = 0; i < length; i++){
        /* Read SPI slave data register */
        while (!spi_is_ready_to_read(&slave)) {}
        spi_read(&slave, (data + i));
    }

    spi_select_slave(&master, &slave_inst, false);
}

// для приемника пакета с данными
// read заполняет packet полученными данными
//void read(){
//    struct data_packet_t test;
//    void* bytes = cast_to_bytes(&test); // аналог чтения данных из радио-модуля

//    copy_to_packet(bytes, &packet);
//}

// read заполняем
int read(){
    if( !spi_is_ready_to_read(&slave) ){
        return BOOL_FALSE;
    }

    //  заполняем command_packet
    uint8_t* bytes = (uint8_t*)(&command_packet);
    recieve_from_radio(bytes, sizeof(command_packet));

    return BOOL_TRUE;
}

// write передает данные из packet
void write(){
    uint8_t* bytes = cast_to_bytes(&packet);
    send_to_radio(bytes, DATA_PACKET_SIZE);
}

//--------------------------------------


//--------------------------------------
//--- Инициализация / опрос датчиков ---
//--------------------------------------
void read_id(){
    packet.id[0] = 0x24;
    packet.id[1] = 0x62;
    packet.id[2] = 0x49;
}

void read_gps(){
    uint8_t fake_gps[83] = "$GPGGA,075241.00,5550.6135,N,03732.2515,E,1,22,0.5,00188.9,M,0014.4,M,,*67\r\n";
    memcpy(packet.gps_data, fake_gps, sizeof(packet.gps_data));
}

void read_radiation(){
    packet.radiation = 123;
}

void read_bat_voltage(){
    packet.bat_voltage = 3.5f;
}

void read_detector_voltage(){
    packet.bat_voltage = 1.2f;
}

// initialize инициализирует устройство.
// задает значения полям структуры packet.
void initialize(){
    set_timer(60);

    read_id();
    read_gps();
    read_radiation();
    read_bat_voltage();
    read_detector_voltage();
}
//--------------------------------------


//--------------------------------------
//-------- Маршрутизация команд --------
//--------------------------------------
void on_get_data(args_buf_t* buf){
    struct args_get_data_t args_get_data = extract_args_get_data(buf);

    if(args_get_data.update_gps){
        read_gps();
    }

    // отправляем пакет с данными
    write();
}

void on_set_timer(args_buf_t* buf){
    struct args_set_timer_t args_set_timer = extract_args_set_timer_t(buf);
    set_timer(args_set_timer.secs);
}

// route_command вызывает функцию, соответствующую коду команды
void route_command(struct command_packet_t* cmd){
    if(cmd == NULL){
        return;
    }

    switch (cmd->command) {
    case COMMAND_GET_DATA:
        on_get_data(&cmd->raw_args);
        break;

    case COMMAND_SET_TIMER:
        on_set_timer(&cmd->raw_args);
        break;
    }
}
//--------------------------------------

int main(){
//    printf("Command %lu \n", sizeof(COMMAND_GET_DATA));
//    printf("packet %lu \n", sizeof(packet));
//    printf("command_packet %lu \n", sizeof(command_packet));

    initialize();

    size_t last_send = 0;
    size_t ticks = 0;

    while(BOOL_TRUE) {
        // считываем команду от станции
        if(read()){
            route_command(&command_packet);
        }

        // отправляем данные по таймеру
        if(ticks - last_send > timer ){
            last_send = ticks;
            write();
        }

        ticks += 1;
    }

    return 0;
}
