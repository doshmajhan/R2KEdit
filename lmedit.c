///
///  Program to read in binary files and
///  write to sections selected by the user
///  @author cpc1007 Cameron Clark
///

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <unistd.h>
#include "exec.h"

#define MAX_LEN 100 //Max length for input

static const char QUIT[] = "quit";
static const char SIZE[] = "size";
static const char WRITE[] = "write";
static const char SECTION[] = "section";
static const char *SECTLIST[] = {"text", "rdata", "data", 
        "sdata", "sbss", "bss", "reltab", "reftab", "symtab", "string" };
static char *cmdList[10];
static int indexLst[10];
static bool edited = false;
static bool load = false;
static int skip;
static int sectNum;

typedef struct hist{
    char *command;
    int index;
}hist;

///
///	 Function to check the section number and return
///	 the corresponding string
///	 x - the section number
///	 returns the correct string
///
char *checkSection(int x){
    char *section = malloc(10);
	
    if(x == EH_IX_TEXT){
        strcpy(section,"text");
    }
    if(x == EH_IX_RDATA){
	    strcpy(section,"rdata");
    }
    if(x == EH_IX_DATA){
	    strcpy(section,"data");
    }
    if(x == EH_IX_SDATA){
        strcpy(section,"sdata");
    }
    if(x == EH_IX_SBSS){
	    strcpy(section,"sbss");
    }
    if(x == EH_IX_BSS){
        strcpy(section,"bss");
    }
    if(x == EH_IX_REL){
        strcpy(section,"reltab");
    }
    if(x == EH_IX_REF){
        strcpy(section,"reftab");
    }
    if(x == EH_IX_SYM){
        strcpy(section,"symtab");
    }
    if(x == EH_IX_STR){
        strcpy(section,"strings");
    }
    return section;
}

///
///  Function to read the header block of the file
///  head - the exec_t struct containing the head block information
///  filename - the name of the binary file entered
///  returns the number of bytes to skip to read valid table
///
int readHead(exec_t *head, char *filename){
    int year, month, day, x;
    int skip = 0;
	char *section;

    if(ntohs(head->magic) != HDR_MAGIC){
        fprintf(stderr, "error: %s is not an R2K object module (magic number 0x%02x)\n"
        , filename, ntohs(head->magic));
        return 0;
    }else{
        if(head->entry == 0){
            printf("File %s is an R2K object module\n", filename);
        }else{
            printf("File %s is an R2K load module (entry point 0x%08X)\n"
            , filename, ntohl(head->entry));
            load = true;
        }
        head->version = ntohs(head->version);
        day = head->version & 0x1f;
        head->version = head->version >> 5;
        month = head->version & 0xf;
        head->version = head->version >> 4;
        year = (head->version & 0x1ff) + 2000;
        printf("Module version: %04d/%02d/%02d\n", year, month, day);
        for( x = 0; x < N_EH; x ++){
            if(head->data[x] != 0){
                section = checkSection(x);
                if((x == EH_IX_REL) || (x == EH_IX_REF) || (x == EH_IX_SYM)){
                    printf("Section %s is %d entries long\n", section, ntohl(head->data[x]));
                }
                else if(x == EH_IX_STR){
                    printf("Section %s is %d bytes long\n", section, ntohl(head->data[x]));
                }
		        else{
		            printf("Section %s is %d bytes long\n", section, ntohl(head->data[x]));
		            skip += ntohl(head->data[x]);
		        }
                free(section);
            }
        }
    }
    return skip;
}

///
///  Function to read a specific index in the string table and convert to ascii
///  strings[] - the list of the bytes in the string table
///  start - the index to start at
///  stop - the index to stop at
///  returns the converted string
///
char *readString(uint8_t strings[], uint32_t start, uint32_t stop){
    char *symbol = malloc(stop - start);
    uint32_t x;
    int i = 1;

    symbol[0] = (char)strings[start];
    for( x = start + 1; x < stop; x ++){
        if(strings[x] == 0x00){
            symbol[i] = (char)strings[x];
            return symbol;
        }
        symbol[i] = (char)strings[x];
        i++;
    }

    return symbol;
}

///
///  Function to read the different tables
///  filecopy - the file to be edited
///  strings - a list of the strings from the string table
///  size - size of the string table
///  returns nothing
///
void readTable(FILE *filecopy, uint8_t strings[], int size){
    char *symbol;
	char *section;

	if(sectNum == EH_IX_REL){
		relent_t *rel = malloc(sizeof(relent_t));
		fread(rel, sizeof(relent_t), 1, filecopy);
		printf("   0x%08x ", ntohl(rel->addr));
		section = checkSection(rel->section - 1);
        printf("(%s) ", section);
        printf("type 0x%04x\n", rel->type);
        free(section);
		free(rel);
	}else if(sectNum == EH_IX_REF){
		refent_t *ref = malloc(sizeof(refent_t));
		fread(ref, sizeof(refent_t), 1, filecopy);
		printf("   0x%08x ", ntohl(ref->addr));
		printf("type 0x%04x ", ref->type);
        symbol = readString(strings, ntohl(ref->sym), size);
        printf("symbol %s\n", symbol);
        free(symbol);
		free(ref);
	}else{
		syment_t *symb = malloc(sizeof(syment_t));
		fread(symb, sizeof(syment_t), 1, filecopy);
		printf("   value 0x%08x ", ntohl(symb->value));
        printf("flags 0x%08x ", ntohl(symb->flags));
        symbol = readString(strings, ntohl(symb->sym), size);
        printf("symbol %s\n", symbol);
        free(symbol);
		free(symb);
	}
}

///
/// Function to edit a certain section in the memory of the file
/// cmd - The command that contains what is to be displayed/edited
/// filecopy - the file to be edited
/// strings - a list of the strings from the string table
/// size - size of the string table
/// head - contains info about the head of the file
/// returns nothing
///
void editSection(char *cmd, FILE *filecopy, uint8_t strings[], int size, exec_t *head){
   
    char *next, *ptr;
    char addr[100];
    char count[100];
    char type[100];
    char value[100];
    
    int countInt = 1;
    uint32_t addrInt, printAddr;
    char s[3] = ",:=";

    int bytes = 4, index, x, max = 0;
    uint32_t read32, write32;
    uint16_t read16, write16;
    uint8_t read8, write8;

    bool countC = false;
    bool typeC = false;
    bool check = true;
    bool single = true;
    bool assigned[4] = {false, false, false, false};

    //Go through the different options to check if they're present
    for(x = 0; x < 3; x ++){
        //find the index of the first occurence of the option char
        next = strchr(cmd, s[x]);
        if(next){
            single = false;
            index = (int)(next - cmd);
            //Assign address value
            if(check){
                check = false;
                assigned[0] = true;
                addr[index] = '\0';
                strncpy(addr, cmd, index);
            //Signal that count needs to be read in
            }if(s[x] == ','){
                countC = true;
            //Signals that type needs to be read in
            }else if(s[x] == ':'){
                typeC = true;
                if(countC){
                    assigned[1] = true;
                    count[index] = '\0';
                    strncpy(count, cmd, index);
                    countC = false;
                }
            //Signals that value needs to be read in
            }else if(s[x] == '='){
                if(typeC){
                    assigned[2] = true;
                    type[index] = '\0';
                    strncpy(type, cmd, index);
                    typeC = false;
                }else if(countC){
                    assigned[1] = true;
                    count[index] = '\0';
                    strncpy(count, cmd, index);
                }
                countC = false;
            }
            //remove the read-in command from the strong
            index++;
            cmd = cmd+index;
        }
    }
    //deal with the option at the end of the string, determining what option it is
    if(single){
        assigned[0] = true;
        addr[strlen(cmd)] = '\0';
        strncpy(addr, cmd, strlen(cmd));
    }else{
        if(countC){
            assigned[1] = true;
            count[strlen(cmd)] = '\0';
            strncpy(count, cmd, strlen(cmd));
        }else if(typeC){
            assigned[2] = true;
            type[strlen(cmd)] = '\0';
            strncpy(type, cmd, strlen(cmd));
        }else{
            assigned[3] = true;
            value[strlen(cmd)] = '\0';
            strncpy(value, cmd, strlen(cmd));
        }
    }

    //Convert to integer values to process
    if(assigned[0]){
        if(load){
            addrInt = strtol(addr, &ptr, 16);
            printAddr = addrInt;
            if(sectNum == EH_IX_TEXT){
                addrInt -= TEXT_BEGIN;
            }else if(sectNum == EH_IX_RDATA){
                addrInt -= DATA_BEGIN;
            }else if(sectNum == EH_IX_DATA){
                addrInt -= (DATA_BEGIN + ntohl(head->data[sectNum - 1]));
            }else{
                addrInt -= STACK_BEGIN;
            }
        }else{
            addrInt = strtol(addr, &ptr, 10);
            printAddr = addrInt;
        }
        if(addrInt > ntohl(head->data[sectNum])){
            if((sectNum == EH_IX_REF) || (sectNum == EH_IX_REL) || (sectNum == EH_IX_SYM)){
                fprintf(stderr, "error:  '%d' is not a valid index\n", addrInt);
            }else{
                fprintf(stderr, "error:  '%d' is not a valid address\n", addrInt);
            }
            return;
        }
    }
    if(assigned[1]){
        countInt = strtol(count, &ptr, 10);
        max = (int)(ntohl(head->data[sectNum]) - addrInt);
        if(load){
            max = max/4;
        }
        if(countInt > max){
            fprintf(stderr, "error:  '%d' is not a valid count\n", countInt);
            return;
        }
    }
    if(assigned[2]){
        if((sectNum == EH_IX_REL) || (sectNum == EH_IX_REF) || (sectNum == EH_IX_SYM)){
            fprintf(stderr, "error:  ':%s' is not a valid in table sections\n", type);
            return;
        }else{
            if(strcmp(type, "b") == 0){
                bytes = 1;
            }
            else if(strcmp(type, "h") == 0){
                bytes = 2;
            }
            else if (strcmp(type, "w") == 0){
                bytes = 4;
            }else{
                fprintf(stderr, "error:  '%s' is not a valid type\n", type);
                return;
            }
        }
    }
    if(assigned[3]){
        if((sectNum == EH_IX_REL) || (sectNum == EH_IX_REF) || (sectNum == EH_IX_SYM)){
            fprintf(stderr, "error:  '=%s' is not valid in table sections\n", value);
            return;
        }else{
            if(bytes == 1){
                write8 = strtol(value, &ptr, 10);
            }else if(bytes == 2){
                write16 = strtol(value, &ptr, 10);
            }else{
                write32 = strtol(value, &ptr, 10);
            }
            edited = true;
        }
    }
   
    fseek(filecopy, (skip + addrInt), SEEK_SET);
    //Edit the file
    if(assigned[3]){
        for(x = 0; x < countInt; x ++){
            if(bytes == 1){
                fwrite(&write8, 1, bytes, filecopy);
                printf("   0x%08x is now 0x%02x\n", printAddr, write8);
            }else if(bytes == 2){
                write16 = ntohs(write16);
                fwrite(&write16, 1, bytes, filecopy);
                printf("   0x%08x is now 0x%04x\n", printAddr, ntohs(write16));
            }else{
                write32 = ntohl(write32);
                fwrite(&write32, 1, bytes, filecopy);
                printf("   0x%08x is now 0x%08x\n", printAddr, ntohl(write32));
            }
        }
    }
    //Just print information
    else{
        for(x = 0; x < countInt; x ++){
			if((sectNum == EH_IX_REL) || (sectNum == EH_IX_REF) || (sectNum == EH_IX_SYM)){
                if((sectNum == EH_IX_REL) && (x == 0)){
                    fseek(filecopy, (skip + (addrInt*sizeof(relent_t))), SEEK_SET);
                }else if((sectNum == EH_IX_REF) && (x == 0)){
                    fseek(filecopy, (skip + (addrInt*sizeof(refent_t))), SEEK_SET);
                }else{
                    if(x == 0){
                        fseek(filecopy, (skip + (addrInt*sizeof(syment_t))), SEEK_SET);
                    }
                }
				readTable(filecopy, strings, size);
			}
			else{
				printf("   0x%08x = ", printAddr);
				if(bytes == 1){
					fread(&read8, bytes, 1, filecopy);
					printf("0x%02x\n", read8);
				}
				else if(bytes == 2){
					fread(&read16, bytes, 1, filecopy);
					printf("0x%04x\n", ntohs(read16));
				}
				else{
					fread(&read32, bytes, 1, filecopy);
					printf("0x%08x\n", ntohl(read32));
				}
                printAddr += bytes;
            }
        }
    }
}

///
///  Function to clear rest of input stream
///  takes in no paramters
///  returns nothing
///
void clear(void){
    while( getchar() != '\n');
}

///
///  Main function
///  Calls essential methods
///
int main(int argc, char *argv[]){
    
    if((argc < 2) || (argc > 2)){
        fprintf(stderr, "usage: lmedit file\n");
        return EXIT_FAILURE;
    }
	
    FILE *fp, *filecopy;
    int x, i, commands, cmdInt, index;

    char *result, *cmd, *temp = NULL, *arg1, *ptr;
    char *answer = malloc(4);
    char *section = malloc(10);
    char line[ MAX_LEN ];

    bool answered = false;
    bool valid = false;
    bool quit = false;
    bool validHistory = false;
    bool empty = false;

    char cpRead[(2*strlen(argv[1]) + 3)];
    char cpWrite[(2*strlen(argv[1]) + 3)];
    char copy[strlen(argv[1]) + 1];
    char remove[strlen(argv[1]) + 1];

    strcpy(copy, argv[1]);
    copy[0] = 'z';
    sprintf(cpRead, "cp %s %s", argv[1], copy);
    sprintf(cpWrite, "cp %s %s", copy, argv[1]);
    sprintf(remove, "rm %s", copy);

    exec_t *head = malloc(sizeof(exec_t));
   
    //Create a copy if the file to edit
    system(cpRead);
    fp = fopen(argv[1], "r+b");
    filecopy = fopen(copy, "r+b");

    if(!fp){
        perror("error: "); 
        printf("Failed to open file '%s'\n", argv[1]);
        free(head);
        return EXIT_FAILURE;
    }
    fclose(fp);
    fread(head, sizeof(exec_t), 1, filecopy);

	//Read in the string table to be stored.
    skip = readHead(head, argv[1]);
	uint8_t strings[ntohl(head->data[EH_IX_STR])] ;
	fseek(filecopy, (skip + (ntohl(head->data[EH_IX_REL]) * sizeof(relent_t)) +
    (ntohl(head->data[EH_IX_REF]) * sizeof(refent_t)) + (ntohl(head->data[EH_IX_SYM]) *
    sizeof(syment_t)) + sizeof(exec_t)), SEEK_SET);
	fread(strings, ntohl(head->data[EH_IX_STR]), 1, filecopy);
	
    if(skip == 0){
        free(head);
        //fclose(fp);
        return EXIT_FAILURE;
    }
    //Start at text section
    skip = sizeof(exec_t);
    fseek(filecopy, skip, SEEK_SET);

    strcpy(section, "text");
    sectNum = 0;
    commands = 1;
    printf("%s[%d] > ", section, commands);
    fflush(stdout);
    
    while( ( result = fgets( line, MAX_LEN, stdin ) ) ){

        cmd = strtok(line, " \n");
        
        if(cmd[0] == '!'){
            index = 0;
            validHistory = false;
            cmd = cmd+1;
            cmdInt = strtol(cmd, &ptr, 10);
            for(i = 0; i < 10; i++){
                if(indexLst[i] == cmdInt){
                    validHistory = true;
                    index = i;
                }
            }
            if(!validHistory){
                if(cmdInt >= commands){
                    fprintf(stderr, "error:  command '%d' has not yet been entered\n", cmdInt);
                }else{
                    fprintf(stderr, "error:  command '%d' is no longer in the command history\n",
                    cmdInt);
                }
                cmd = NULL;
            }else{
                cmd = cmdList[index];
            }
        }
        if(cmd == NULL){
            empty = true;
        }
        else if(feof(stdin)){
            break;
        }
        else if(strcmp(cmd, "history") == 0){
            for(i = 0; i < 10; i++){
                if(cmdList[i] != NULL){
                    printf("   %d  %s\n", indexLst[i], cmdList[i]);
                }
            }
        }
        //Section command
        else if(strcmp(cmd, SECTION) == 0){
            skip = sizeof(exec_t);
            arg1 = strtok( NULL, "\n");
            for( x = 0; x < N_EH; x ++){
                if(strcmp(arg1, SECTLIST[x]) == 0){
                    if((x == 4) || (x == 5)){
                        fprintf(stderr, "error: cannot edit %s section\n", arg1);
                        //So it won't say it was a invalid section after loop
                        valid = true;
                    }
                    else{
                        free(section);
                        section = malloc(10);
                        strcpy(section, SECTLIST[x]);
                        sectNum = x;
                        fseek(filecopy, skip, SEEK_SET);
                        printf("Now editing section %s\n", section);
                        valid = true;
                        break;
                    }
                }
                if(x == EH_IX_REL){
                    skip += (ntohl(head->data[x]) * sizeof(relent_t));
                }else if(x == EH_IX_REF){
                    skip += (ntohl(head->data[x]) * sizeof(refent_t));
                }else if(x == EH_IX_SYM){
                    skip += (ntohl(head->data[x]) * sizeof(syment_t));
                }else{
                    skip += ntohl(head->data[x]);
                }
            }
            if(!valid){
                fprintf(stderr, "error: '%s' is not a valid section name\n", arg1);
            }
            valid = false;
        }
        //Size command
        else if(strcmp(cmd, SIZE) == 0){
            if(head->data[sectNum] != 0){
                if((sectNum == EH_IX_REL) || (sectNum == EH_IX_REF) || (sectNum == EH_IX_SYM)){
                    printf("Section %s is %d entries long\n", section, ntohl(head->data[sectNum]));
                }
		        else{
		            printf("Section %s is %d bytes long\n", section, ntohl(head->data[sectNum]));
		        }
            }
        }
        //Write command
        else if(strcmp(cmd, WRITE) == 0){
            if(edited){
                system(cpWrite);
            }
            edited = false;
        }
        //Quit command
        else if(strcmp(cmd, QUIT) == 0){
            if(edited){
                while(!answered){
                    printf("Discard modifications (yes or no)? ");
                    scanf("%3s", answer);
                    if(strcmp(answer, "yes") == 0){
                        quit = true;
                        break;
                    }else if(strcmp(answer, "no") == 0){
                        answered = true;
                    }
                    clear();
                }
                if(quit){
                    break;
                }
            }else{
                break;
            }
            answered = false;
        }
        //Edit section
        else{
            editSection(cmd, filecopy, strings, ntohl(head->data[EH_IX_STR]), head);
        }
        
        //Bonus
        if(!empty){
            if(temp != NULL){
                free(temp);
            }
            temp = malloc(strlen(cmd) + 1);
            strcpy(temp, cmd);
            for(i = 0; i < 9; i++){
                if(cmdList[i] != NULL){
                    free(cmdList[i]);
                }
                if(cmdList[i+1] != NULL){
                    cmdList[i] = malloc(strlen(cmdList[i+1]) + 1);
                    strcpy(cmdList[i], cmdList[i+1]);
                }
                indexLst[i] = indexLst[i+1];
            }
            if(cmdList[9] != NULL){
                free(cmdList[9]);
            }
            cmdList[9] = malloc(strlen(temp) + 1);
            strcpy(cmdList[9], temp);
            indexLst[9] = commands;
        }else{
            empty = false;
        }
    
        commands++;
        printf("%s[%d] > ", section, commands);
        fflush(stdout);
    }

    for(i = 0; i < 10; i ++){
        if(cmdList[i] != NULL){
            free(cmdList[i]);
        }
    }
    if(temp){
        free(temp);
    }
    fclose(filecopy);
    system(remove);
    free(head);
    free(section);
    free(answer);

    return EXIT_SUCCESS;
}
