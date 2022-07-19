#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>

//HW Consts
#define MEMORY_DEPTH 4096
#define MEMORY_WIDTH 32
#define REG_NUMBER 16
#define IRQ2_LINE_SIZE 11
#define IRQ2_DEPTH 100 
#define IO_REG_NUM 23
#define IO_REG_NAME 30
#define DISK_SIZE 16384
#define DISK_TIME 1024
#define SECTOR_SIZE 128
#define FRAME_ROWS 256
#define FRAME_COLS 256



//File consts
#define MEMIN 1
#define DISKIN 2
#define IRQ2IN 3
#define MEMOUT 4
#define REGOUT 5
#define TRACE 6
#define HWREGTRACE 7
#define CYCLES 8
#define LEDS 9
#define DISPLAY7SEG 10
#define DISKOUT 11
#define MONITOR 12
#define MONITOR_YUV 13


//Struct that will hold all IO hardware data
typedef struct IO_HW { 
	int mem_flag;
	int interrupted;
	FILE* hwregtrace_file;
	FILE* leds_file;
	FILE* display7seg_file;
	int32_t IO_reg_arr[IO_REG_NUM];

	int32_t clk;
	
	uint32_t irq2_val[IRQ2_DEPTH];
	int curr_irq2;

	int32_t disk[DISK_SIZE];
	int diskin_size;
	int disk_time;

	int32_t display7seg_curr;

	int32_t frame_buffer[FRAME_ROWS][FRAME_COLS];

}IO_HW;

// Struct that will use for decoding instruction
typedef struct command {
	int opcode, rd, rs, rt, imm, Itype,LS;
}command;

//Decoding function
command decode(int32_t cmd) {
	command com;
	com.imm = 0;
	com.Itype = 0;
	com.rt = cmd & 0xf;
	cmd >>= 4;
	com.rs = cmd & 0xf;
	cmd >>= 4;
	com.rd = cmd & 0xf;
	cmd >>= 4;
	com.opcode = cmd & 0xff;
		
	return com;
}


         ///////all functions declared////////

//Helper fuctions//
command decode(int32_t cmd);
void reset_reg(int32_t regArr[], int ArrSize);
int inst_type(command com);
int load_save(command com);
int sign_ext(int num, int msb);
int char2int(char c);

//Load functions//
void read_irq2in(IO_HW* hardware, char* irq2_file);
int read_memin(int32_t mem_arr[], char* p);
void IO_init(IO_HW* hardware, char* diskin, char* irq2in);

//Execution functions//
void exe_inst(int32_t mem[], int32_t R[], int inst_num, IO_HW* hardware, FILE* trace);
int exe_opcode(command com, int32_t mem[], int32_t R[], int pc, IO_HW* hardware);

//Interupt functions//
void irq0(IO_HW* hardware);
void irq1(IO_HW* hardware);
void irq2(IO_HW* hardware);
int intterupted(int pc, IO_HW* hardware);

//Hardware functions//
void clk(IO_HW* hardware, command* com);
void disk_exe(int32_t mem[], IO_HW* hardware);
void read_sector(int32_t mem[], IO_HW* hardware);
void write_sector(int32_t mem[], IO_HW* hardware);
void monitor_cmd(IO_HW* hardware);

//Out functions//
void trace_writer(FILE* trace, int pc, int32_t cmd, int32_t R[]);
void memout(char* file, int32_t mem[], int inst_num, IO_HW* hardware);
void diskout(char* file, IO_HW* hardware);
void regout(char* file, int32_t R[]);
void hwregtrace_writer(IO_HW* hardware, int read, int name, int32_t data);
void cycles(char* file, IO_HW* hardware);
void display7seg(IO_HW* hardware);
void monitor_writer(char* file, IO_HW* hardware);
void monitor_writer_yuv(IO_HW* hardware, char* file);

///////////////////////////////////////////////////
        /////The full functions/////
///////////////////////////////////////////////////


          ///Help functions///

void reset_reg(int32_t regArr[], int ArrSize) {
	int i;
	for (i = 0; i < ArrSize; i++) {
		regArr[i] = 0;
	}
}

int inst_type(command com) {//*need to check if deals with all cases
	if (((com.opcode >= 0) && (com.opcode <= 8)) || (com.opcode == 16) || (com.opcode == 19)) {
		if ((com.rs == 1) || (com.rt == 1)) {
			com.Itype =1;
		}
	}

	else if (((com.opcode >= 9) && (com.opcode <= 14)) || (com.opcode == 17) || (com.opcode == 20)) {
		if ((com.rs == 1) || (com.rt == 1) || (com.rd == 1)) {
			com.Itype = 1;
		}
	}

	else if (com.opcode == 15) {
		if (com.rs == 1) {
			com.Itype = 1;
		}
	}
	return com.Itype;
}

int load_save(command com) {
	int ls = 0;
	if ((com.opcode == 16) || (com.opcode == 17)) {
		ls = 1;
	}
	return ls;
}

int sign_ext(int num, int msb) {
	int sign = 0x1 << msb;
	if ((num & sign) > 0) {
		num |= (0xffffffff << msb);
	}
	return num;
}

int char2int(char c) {
	int c_val = 0;
	if (c >= '0' && c <= '9') {
		c_val = c - '0';
	}
	else if (c >= 'a' && c <= 'f') {
		c_val = c - 'a' + 10;
	}
	else if (c >= 'A' && c <= 'F') {
		c_val = c - 'A' + 10;
	}
	return(c_val);
}



///Load functions///

//reads into the hardware the irq2 preset in thr input file irq2in
void read_irq2in(IO_HW* hardware, char* irq2_file ) {
	FILE* irq2_p;
	uint32_t irq2_tmp;
	irq2_p = fopen(irq2_file, "r");
	if (irq2_p == NULL) {
		printf("could not open imemin file for reading");
		exit(1);
	}
	char line[IRQ2_LINE_SIZE];
	int i = 0, j = 0;
	while (fgets(line, 10, irq2_p)) {
		irq2_tmp = 0;
		for (int j = 0; line[j] != '\n' && line[j] != '\0'; j++) {
			irq2_tmp = irq2_tmp *10;
			irq2_tmp += char2int(line[j]);
		}
		hardware->irq2_val[i] = irq2_tmp;
		i++;

	}
	fclose(irq2_p);
	return ;

}

// reads both from memin and diskin data 
int read_memin(int32_t mem_arr[], char* p) {
	FILE* fp;
	fp = fopen(p, "r");
	if (fp == NULL) {
		printf("could not open imemin file for reading");
		exit(1);
	}
	char line[10];
	int i = 0, j = 0;
	int32_t cmd_val ;
	while (fgets(line, 10, fp)) {
		cmd_val = 0;
		for (int j = 0; line[j] != '\n' && line[j] != '\0'; j++) {
			cmd_val <<= 4;
			cmd_val |= char2int(line[j]);
		}
		mem_arr[i] = cmd_val;
		i++;
		
	}
	fclose(fp);
	return i;
}

//Initalizes the I/O hardware that holds HW I/O regs and all information for I/O access
void IO_init(IO_HW* hardware, char* diskin, char* irq2in, char* hwregtrace,char* leds_file ,char* display7seg) { //*need to add input files and more handling
	hardware->interrupted = 0;
	hardware->curr_irq2 = 0;
	hardware->disk_time = 0;
	hardware->clk = 0;
	hardware->display7seg_curr = 0;
	hardware->mem_flag = 0;
	hardware->diskin_size = read_memin(hardware->disk, diskin);
	read_irq2in(hardware, irq2in);
	
	hardware->hwregtrace_file = fopen(hwregtrace, "w");//open hwregtrace.txt file for writing
	if (hardware->hwregtrace_file == NULL) {
		printf("could not open hwregtrace file for reading");
		exit(1);
	}
	hardware->leds_file = fopen(leds_file, "w");//open leds.txt file for writing
	if (hardware->leds_file == NULL) {
		printf("could not open leds file for reading");
		exit(1);
	}
	hardware->display7seg_file = fopen(display7seg, "w");//open hwregtrace.txt file for writing
	if (hardware->display7seg_file == NULL) {
		printf("could not open display7seg file for reading");
		exit(1);
	}
	//reset frame buffer
	for (int i = 0; i < FRAME_ROWS; i++) {
		for (int j = 0; j < FRAME_COLS; j++) {
			hardware->frame_buffer[i][j] = 0;
		}
	}
	reset_reg(hardware->IO_reg_arr, IO_REG_NUM);//reset regs to zero



}
///Execution functions///

//This function does the main execution handling (the main loop of the pcu)
void exe_inst(int32_t mem[], int32_t R[], int inst_num, IO_HW* hardware, FILE* trace) {
	int halt = 0;
	int pc = 0;
	int i_type;
	int32_t imm = 0;
	command com;
	int32_t cmd;
	
	//Main Fetch - Decode loop
	while ((halt == 0) && (pc < inst_num)) {
		
		irq0(hardware);
		irq1(hardware);
		irq2(hardware);
		pc = intterupted(pc, hardware);
		cmd = mem[pc];
		com = decode(cmd);
		com.Itype = inst_type(com);
		com.LS = load_save(com);
		if (com.Itype == 1) {
			com.imm = sign_ext(mem[pc + 1], 19);
		}
		//make sure special reg as their value
		R[0] = 0;
		R[1] = com.imm;
		trace_writer(trace, pc, cmd, R);
		
		pc = exe_opcode(com, mem, R, pc, hardware);//Go to exe function of the instruction
		
		display7seg(hardware);
		if (pc == -1) halt = 1;//halt program when pc == -1
		
		//Timing for next cycle
		clk(hardware, &com);
		
	}
}

//Executes the instruction according to the inst opcode (exe 1 inst each time)
int exe_opcode(command com, int32_t mem[], int32_t R[], int pc, IO_HW * hardware) {
	int opcode = com.opcode, rd = com.rd, rs = com.rs, rt = com.rt;
	int32_t imm = com.imm;
	int in_idx;
	int32_t IO_reg;
	int sw_idx;


	switch (opcode)
	{
	case 0: //add
		R[rd] = R[rs] + R[rt];
		break;
	case 1: //sub
		R[rd] = R[rs] - R[rt];
		break;
	case 2: //mul
		R[rd] = R[rs] * R[rt];
		break;
	case 3: //and
		R[rd] = R[rs] & R[rt];
		break;
	case 4: //or
		R[rd] = R[rs] | R[rt];
		break;
	case 5: //xor
		R[rd] = R[rs] ^ R[rt];
		break;
	case 6: //sll
		R[rd] = R[rs] << R[rt];
		break;
	case 7: //sra
		R[rd] = R[rs] >> R[rt];
		if (R[rs] < 0) {
			sign_ext(R[rd], 31 - R[rt]);
		}
		break;
	case 8://srl
		R[rd] = R[rs] >> R[rt];
		break;
	case 9://beq
		if (R[rs] == R[rt]) return R[rd];
		break;
	case 10://bne
		if (R[rs] != R[rt]) return R[rd];
		break;
	case 11://blt
		if (R[rs] < R[rt]) return R[rd];
		break;
	case 12://bgt
		if (R[rs] > R[rt]) return R[rd];
		break;
	case 13://ble
		if (R[rs] <= R[rt]) return R[rd];
		break;
	case 14://bge
		if (R[rs] >= R[rt]) return R[rd];
		break;
	case 15://jal
		R[rd] = pc + (1 + com.Itype);
		R[0] = 0;
		R[1] = imm;
		return R[rs];
		break;
	case 16://lw 
		R[rd] = sign_ext(mem[R[rs] + R[rt]], 19);
		break;
	case 17: //sw 
		sw_idx = R[rs] + R[rt];
		mem[sw_idx] = R[rd] & 0xfffff;
		if (sw_idx > hardware->mem_flag) {
			hardware->mem_flag = sw_idx;
		}
		break;
	case 18://reti 
		hardware->interrupted = 0;
		pc = hardware->IO_reg_arr[7];
		break;
	case 19: //in 
		in_idx = R[rt] + R[rs];
		if ((in_idx == 18) || (in_idx == 19)) { //Saved regs for future use
			R[rd] = 0;
		}
		else {
			R[rd] = hardware->IO_reg_arr[in_idx];
			// Trace IO_re data as READ
			hwregtrace_writer(hardware, 1, in_idx, hardware->IO_reg_arr[in_idx]);
		}	
		break;
	case 20: //out   
		IO_reg = R[rs] + R[rt];
		//check special conditions to write to the IO_regs
		if (IO_reg == 9)// write to leds
		{
			hardware->IO_reg_arr[IO_reg] = R[rd]; //update register leds with the value in register rd
			int32_t  leds = hardware->IO_reg_arr[IO_reg]; //take the new value in register leds 
			fprintf(hardware->leds_file, "%u %08X\n", hardware->clk, leds); //write into the file leds
			hwregtrace_writer(hardware, 0, IO_reg, hardware->IO_reg_arr[IO_reg]);
		}
		else if (IO_reg == 22 && R[rd] == 1)//write to monitor
		{
			hardware->IO_reg_arr[IO_reg] = 1;
			hwregtrace_writer(hardware, 0, IO_reg, hardware->IO_reg_arr[IO_reg]);
			monitor_cmd(hardware);
			
		}
		else if (IO_reg == 8 || IO_reg == 17)// write/read disk while busy
		{
			printf("Registers are unavailble for writing");//disk status and clk are read only.
		}
		else if ((IO_reg == 14 || IO_reg == 15 || IO_reg == 16) && hardware->IO_reg_arr[17] == 1)
		{
			printf("Disk is busy");//disk is busy
		}
		else if ((IO_reg == 14) && (R[rd] == 1 || R[rd] == 2))//Execute disk action
		{
			hardware->IO_reg_arr[17] = 1;
			hardware->disk_time = 0;
			hardware->IO_reg_arr[IO_reg] = R[rd];
			disk_exe(mem, hardware);
			hwregtrace_writer(hardware, 0, IO_reg, hardware->IO_reg_arr[IO_reg]);
		}
		else
		{
			hardware->IO_reg_arr[IO_reg] = R[rd];
			//Trace data as WRITE
			hwregtrace_writer(hardware, 0, IO_reg, hardware->IO_reg_arr[IO_reg]);
		}
		break;
	case 21://halt 
		return -1;
		break;
	}
	R[0] = 0;
	R[1] = imm;
	pc += (1 + com.Itype);
	return pc;
	}

	

///Interuption functions///

//Interuption managing function
int intterupted(int pc, IO_HW* hardware) {
	int new_pc = pc;
	int irq = 0;
	irq = (hardware->IO_reg_arr[0] & hardware->IO_reg_arr[3]) |
		(hardware->IO_reg_arr[1] & hardware->IO_reg_arr[4]) |
		(hardware->IO_reg_arr[2] & hardware->IO_reg_arr[5]);
	if ((hardware->interrupted == 0) & (irq == 1)) {
		new_pc = hardware->IO_reg_arr[6];
		hardware->IO_reg_arr[7] = pc - 1;
		hardware->interrupted = 1;
	}
	return(new_pc);
}

//IRQ0 handling function (timer)
void irq0(IO_HW* hardware) {
	int enabaled = hardware->IO_reg_arr[11];
	int timer_current = hardware->IO_reg_arr[12];
	int timer_max = hardware->IO_reg_arr[13];
	if (enabaled == 1) {
		timer_current += 1;
		if (timer_current == timer_max) {
			hardware->IO_reg_arr[3] = 1;
			hardware->IO_reg_arr[12] = 0;
		}
		else {
			hardware->IO_reg_arr[12] = timer_current;
		}
	}
	
}

//IRQ1 handling function (disk)
void irq1(IO_HW* hardware) {
	if ((hardware->IO_reg_arr[17] == 1) && (hardware->disk_time == DISK_TIME )) { //finshed disk exe
		hardware->IO_reg_arr[14] = 0;
		hardware->IO_reg_arr[17] = 0;
		hardware->IO_reg_arr[4] = 1;
		hardware->disk_time = 0;
	}
	
}

//IRQ2 handling fuction (irq2in)
void irq2(IO_HW* hardware) {
	hardware->IO_reg_arr[5] = 0;
	if (hardware->clk > hardware->irq2_val[hardware->curr_irq2]) {
		hardware->IO_reg_arr[5] = 1;
		
		//printf("irq2 :%u in cycle: %u, irq2status is: %d\n", hardware->irq2_val[hardware->curr_irq2], hardware->clk, hardware->IO_reg_arr[5]);
		hardware->curr_irq2++;
	}
	

}

///IO Reg functions///

//deals with timing (*not finished)
void clk(IO_HW* hardware, command* com) {
	int clock = hardware->clk;
	clock = clock + (1 + com->Itype + com->LS);
	if (hardware->IO_reg_arr[17] == 1) {
		hardware->disk_time = hardware->disk_time + (1 + com->Itype + com->LS);
	}
	hardware->clk = clock;
	hardware->IO_reg_arr[8] = (clock&0xFFFFFFFF);
}



//Manage the type of exe with the disk
void disk_exe(int32_t mem[], IO_HW* hardware) {
	int cmd = hardware->IO_reg_arr[14];
	if (cmd == 0) {
		return;
	}
	if (cmd == 1) {
		read_sector(mem, hardware);
	}
	else if (cmd == 2) {
		write_sector(mem, hardware);
	}
}

//reads disk sector into memory
void read_sector(int32_t mem[], IO_HW* hardware) {
	int i;
	int disk_adr = 128 * (hardware->IO_reg_arr[15]);//sector
	int mem_adr = hardware->IO_reg_arr[16];//buffer
	for (i = 0; i < SECTOR_SIZE; i++) {
		mem[mem_adr] = hardware->disk[disk_adr];//read the data from disk to mem
		//read next word
		disk_adr++;
		mem_adr++;
	}
}

//writes data from mem into disk sector
void write_sector(int32_t mem[], IO_HW* hardware) {
	int i;
	int disk_adr = 128 * (hardware->IO_reg_arr[15]);//sector
	int mem_adr = hardware->IO_reg_arr[16];//buffer
	for (i = 0; i < SECTOR_SIZE; i++) {
		 hardware->disk[disk_adr] = mem[mem_adr] ;//write data from mem to disk sector
		//read next word
		disk_adr++;
		mem_adr++;
	}
}

//Monitor data managing functions
void monitor_cmd(IO_HW* hardware) {
	int addr = hardware->IO_reg_arr[20];
	int data = hardware->IO_reg_arr[21];
	int row, col;
	row = addr / FRAME_ROWS;
	col = addr - row * FRAME_COLS;
	hardware->frame_buffer[row][col] = data;
	hardware->IO_reg_arr[22] = 0; //set monitorcmd to 0 after we wrote to frame_buffer
}


                    //OUT data functions//

//trace.txt out data writig function
void trace_writer(FILE* trace, int pc, int32_t cmd, int32_t R[]) {
	int i;
	fprintf(trace, "%03X %05X",pc, cmd);//write for instruction pc and full command
	for (i = 0; i < REG_NUMBER; i++) {//write out each reg value
		fprintf(trace, " %08X", R[i]);
	}
	fprintf(trace, "\n");
}

//hwregtrace.txt out data writig function
void hwregtrace_writer(IO_HW* hardware,int read,int name, int32_t data ) {
	char IO_names[IO_REG_NUM][IO_REG_NAME] = {"irq0enable", "irq1enable", "irq2enable", "irq0status", "irq1status",
															 "irq2status", "irqhandler", "irqreturn", "clks", "leds", "display7seg",
															 "timerenable", "timercurrent", "timermax", "diskcmd", "disksector", "diskbuffer",
	
														 "diskstatus", "reserved", "reserved", "monitoraddr", "monitordata", "monitorcmd" };
	if (read == 1) {
		fprintf(hardware->hwregtrace_file, "%u READ %s %08X\n", hardware->clk, IO_names[name], data);
	}
	else {
		fprintf(hardware->hwregtrace_file, "%u WRITE %s %08X\n", hardware->clk + 1, IO_names[name], data);
	}
	
}


//memout.txt out data writig function
void memout(char* file, int32_t mem[],int inst_num, IO_HW* hardware) {
	FILE* memout = fopen(file, "w");
	if (memout == NULL) {
		printf("could not open memout file for writing");
		exit(1);
	}
	int i;
	int max_mem = inst_num;
	if (max_mem < hardware->mem_flag) {
		max_mem = hardware->mem_flag;
	}
	for (i = 0; i < max_mem ; i++) {
		fprintf(memout, "%05X\n", mem[i]);	
	}
	fclose(memout);
}



//diskout.txt out data writig function
void diskout(char* file, IO_HW* hardware) {
	FILE* diskout = fopen(file, "w");
	if (diskout == NULL) {
		printf("could not open diskout file for writing");
		exit(1);
	}
	int i;
	for (i = 0; i < DISK_SIZE ; i++) {
		fprintf(diskout, "%05X\n", hardware->disk[i]);
	}
	fclose(diskout);
}


//regout.txt out data writig function
void regout(char* file, int32_t R[]) {
	FILE* regout = fopen(file, "w");
	if (regout == NULL) {
		printf("could not open regout file for writing");
		exit(1);
	}
	int i;
	for (i = 2; i < REG_NUMBER;i++) {
		fprintf(regout, "%08X\n", R[i]);
	}
	fclose(regout);
}


//cycles.txt out data writig function
void cycles(char* file, IO_HW* hardware) {
	FILE* cycles = fopen(file, "w");
	if (cycles == NULL) {
		printf("could not open cycles file for writing");
		exit(1);
	}
	fprintf(cycles, "%u\n", hardware->clk);
	fclose(cycles);
}


//display7seg.txt out data writig function
void display7seg(IO_HW* hardware) {
	if (hardware->IO_reg_arr[10] != hardware->display7seg_curr) {
		hardware->display7seg_curr = hardware->IO_reg_arr[10];
		fprintf(hardware->display7seg_file, "%u %08X\n", hardware->clk + 1, hardware->IO_reg_arr[10]);
	}
}



//monitor.txt out data writig function
void monitor_writer(char* file, IO_HW* hardware) {
	FILE* monitor = fopen(file, "w");
	if (monitor == NULL) {
		printf("could not open monitor file for writing");
		exit(1);
	}
	for (int i = 0; i < FRAME_COLS; i++) {
		for (int j = 0; j < FRAME_ROWS; j++) {
			fprintf(monitor, "%02X\n", hardware->frame_buffer[j][i]);
		}
	}
	fclose(monitor);
}


//monitor.yuv out data writig function
void monitor_writer_yuv(IO_HW* hardware, char* file) {
	uint8_t monitor_data[FRAME_ROWS * FRAME_COLS]; //we have 256*256 bytes in the monitor
	FILE* monitor_yuv = fopen(file, "wb"); //open monitoryuv file
	if (monitor_yuv == NULL) {
		printf("could not open monitor file for writing");
		exit(1);
	}

	for (int row = 0; row < FRAME_ROWS; row++) {
		for (int col = 0; col < FRAME_COLS; col++) {
			monitor_data[row * FRAME_ROWS + col] = hardware->frame_buffer[row][col];
		}
	}
	fwrite(monitor_data, 1, FRAME_ROWS * FRAME_COLS, monitor_yuv);
	fclose(monitor_yuv);
}


//The main function
void main(int argc, char** argv) {
	//Init all relevant parameters
	IO_HW hardware;
	int32_t reg[REG_NUMBER] = { 0 };
	int32_t* mem[MEMORY_DEPTH] = { 0 };
	FILE* trace;
	FILE* hwregtrace;
	int inst_num = 0;
	inst_num = read_memin(mem,argv[1]);

	// intialize IO hardware data and load files
	IO_init(&hardware, argv[DISKIN], argv[IRQ2IN],argv[HWREGTRACE],argv[LEDS], argv[DISPLAY7SEG]);// intialize IO hardware data and load files

	trace = fopen(argv[TRACE], "w");//open trace.txt file for writing
	if (trace == NULL) {
		printf("could not open trace file for reading");
		exit(1);
	}
	exe_inst(mem,reg, inst_num ,&hardware, trace);//Fetch - Decode cpu loop

	//write out data
	memout(argv[4], mem ,inst_num, &hardware);
	regout(argv[REGOUT], reg);
	cycles(argv[CYCLES], &hardware);
	diskout(argv[DISKOUT], &hardware);
	monitor_writer(argv[MONITOR], &hardware);
	monitor_writer_yuv(&hardware, argv[MONITOR_YUV]);

	//close all open files
	fclose(trace);
	fclose(hardware.hwregtrace_file);
	fclose(hardware.leds_file);
	fclose(hardware.display7seg_file);
	return ;
}