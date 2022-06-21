#include "fat32.h"
#include <iostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
using namespace std;

void ls(vector<string>& args){
    cout << "list directory" << endl;
}

void mkdir(vector<string>& args){
    cout << "make directory" << endl;
}

void touch(vector<string>& args){
    cout << "create directory" << endl;
}

void mv(vector<string>& args){
    cout << "move directory" << endl;
}

void cat(vector<string>& args){
    cout << "read directory" << endl;
}

string CurrentDirectory = "/> ";
uint32_t CurrentDirectoryFirstCluster; // root cluster at the beginning

uint16_t BytesPerSector;       // Bytes per logical sector (It is always will be 512 in our case). Size: 2 bytes
uint8_t SectorsPerCluster;     // Logical sectors per cluster in the order of two. Size: 1 byte
uint16_t ReservedSectorCount;  // Count of reserved logical sectors. Size: 2 bytes
uint8_t NumFATs;               // Number of file allocation tables. Default value is two but can be higher. Size: 1 byte
uint16_t RootEntryCount;       // Maximum number of FAT12 or FAT16 directory entries. It is 0 for FAT32. Size: 2 bytes
uint16_t TotalSectors16;       // Total logical sectors. It is 0 for FAT32. Size: 2 bytes
uint8_t Media;                 // Media descriptor. Size: 1 byte
uint32_t TotalSectors32;       // Total logical sectors including the hidden sectors
uint32_t FATSize;
uint32_t FirstDataSector;
uint32_t RootCluster;
int fd;

size_t split(const string &txt, vector<string> &strs, char ch)
{
    size_t pos = txt.find( ch );
    size_t initialPos = 0;
    strs.clear();

    // Decompose statement
    while( pos != string::npos ) {
        strs.push_back( txt.substr( initialPos, pos - initialPos ) );
        initialPos = pos + 1;

        pos = txt.find( ch, initialPos );
    }

    // Add the last one
    strs.push_back( txt.substr( initialPos, std::min( pos, txt.size() ) - initialPos + 1 ) );

    return strs.size();
}

int parse(string command, vector<string>& args){
    split(command,args, ' ');
    if(!args[0].compare("cd"))
        return 1;
    else if(!args[0].compare("ls"))
        return 2;
    else if(!args[0].compare("mkdir"))
        return 3;
    else if(!args[0].compare("touch"))
        return 4;
    else if(!args[0].compare("mv"))
        return 5;
    else if(!args[0].compare("cat")){
        return 6;
    }
    else
        return 0;
}




uint32_t FirstSectorofCluster(int N){
    return ((N - 2)*SectorsPerCluster) + FirstDataSector; 
}

void print(FatFileEntry *entry){
    if(entry->lfn.attributes == 15){
        printf("Seq: %x\n",entry->lfn.sequence_number);
        for(int i=0;i<13;i++){
            if(i<5){
                if(entry->lfn.name1[i] == 0)
                    break;
                cout << (char) entry->lfn.name1[i];
            }
            else if(i<11){
                if(entry->lfn.name2[i-5] == 0)
                    break;
                cout << (char) entry->lfn.name2[i-5];
            }
            else{
                if(entry->lfn.name3[i-11] == 0)
                    break;
                cout << (char) entry->lfn.name3[i-11];
            }
        }
        cout << endl;
    }
    else{
        if(entry->msdos.filename[0] == 0)
            return;
        printf("%x\n",entry->msdos.filename[0]);
    }
    
}

void cd(vector<string>& args){
    //parse the path
    vector<string> path;
    split(args[1],path,'/');
    //search the first cluster of current directory
    lseek(fd,FirstSectorofCluster(CurrentDirectoryFirstCluster)*BytesPerSector,SEEK_SET);
    
    

    //CurrentDirectory.insert(CurrentDirectory.size() -2, args[1] + '/');
}




int main(){
    string command;
    BPB_struct bpb;
    void* img;
    //FILE *fp = fopen("../../../example.img","w+");
    //if(fp == NULL) cout << "image does not open" << endl;
    fd = open("../../../example.img",O_RDONLY);
    if(fd == -1) cout << "image does not open" << endl;

    img = mmap(NULL,512,PROT_READ,MAP_SHARED,fd,0);
    if(img == MAP_FAILED) cout << "map failed" << endl;

    memcpy(&bpb,img,sizeof(BPB_struct));

    ReservedSectorCount = bpb.ReservedSectorCount;
    BytesPerSector = bpb.BytesPerSector;
    cout << BytesPerSector << endl;
    cout << "FAT Tables starts at byte offset " << ReservedSectorCount*BytesPerSector << endl; 

    NumFATs = bpb.NumFATs;
    FATSize = bpb.extended.FATSize;

    //void *fat_address = mmap(NULL,NumFATs*FATSize,PROT_READ | PROT_WRITE,MAP_SHARED,fd,ReservedSectorCount*BytesPerSector);
    //cout << *(fat_address) << endl;
    cout << "Data region starts at byte offset " << ReservedSectorCount*BytesPerSector + (NumFATs*FATSize)*BytesPerSector << endl;

    RootCluster = bpb.extended.RootCluster;
    cout << "Root cluster: " << RootCluster << endl;

    FirstDataSector = bpb.ReservedSectorCount + (NumFATs*FATSize);
    cout << "FirstDataSector: " << FirstDataSector << endl;

    uint32_t firstsofc = FirstSectorofCluster(RootCluster); 
    cout << "First Sector of Root Cluster: " << firstsofc << endl; 
    FatFileEntry file;

    //cout << FAT_getFatEntry(fd,RootCluster)<< endl;



    lseek(fd,ReservedSectorCount*BytesPerSector + (NumFATs*FATSize)*BytesPerSector,SEEK_SET);
    while(0){
        FatFileEntry entry2;
        read(fd,&entry2,sizeof(entry2));
        //print(&entry2);
    }
    CurrentDirectoryFirstCluster = RootCluster;
    while(1){
        cout << CurrentDirectory;
        getline(cin,command);
        if(command.compare("quit") == 0){
            break;
        }
        vector<string> args;
        int c = parse(command, args);
        switch (c)
        {
        case 1: // cd
            cd(args);
            break;
        case 2: // ls
            ls(args);
            break;
        case 3: // mkdir
            mkdir(args);
            break;
        case 4: // touch
            touch(args);
            break;
        case 5: // mv
            mv(args);
            break;
        case 6: // cat
            cat(args);
            break;
        }
    }
    close(fd);
    return 0;
}