#include "fat32.h"
#include <iostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <ctime>

#define convert(value) ((0x000000ff & value) << 24) | ((0x0000ff00 & value) << 8) | \
                       ((0x00ff0000 & value) >> 8) | ((0xff000000 & value) >> 24)

using namespace std;


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

string CurrentDirectory = "/";
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
uint32_t EntriesPerCluster;
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


uint32_t getFatEntry(uint32_t clusterNum){
    lseek(fd,ReservedSectorCount*BytesPerSector+clusterNum*4,SEEK_SET);
    uint32_t entry;
    read(fd,&entry,sizeof(entry));
    return entry;
}


uint32_t FirstSectorofCluster(int N){
    return ((N - 2)*SectorsPerCluster) + FirstDataSector; 
}

uint8_t getSequence(FatFileLFN *lfn){
    uint8_t seq = lfn->sequence_number;
    seq = seq << 4;
    seq = seq >> 4;
    return seq;
}

void print(FatFileEntry *entry){
    if(entry->lfn.attributes == 15){
        printf("Seq: %x\n",getSequence(&entry->lfn));
        string name;
        for(int i=0;i<13;i++){
            if(i<5){
                if(entry->lfn.name1[i] == 0)
                    break;
                char a = (char) entry->lfn.name1[i];
                //name.insert(0,&a );
                cout << (char) entry->lfn.name1[i];
            }
            else if(i<11){
                if(entry->lfn.name2[i-5] == 0)
                    break;
                char a = (char) entry->lfn.name2[i-5];
                //name.insert(0,&a );
                cout << (char) entry->lfn.name2[i-5];
            }
            else{
                if(entry->lfn.name3[i-11] == 0)
                    break;
                char a = (char) entry->lfn.name3[i-11];
                //name.insert(0,&a );
                cout << (char) entry->lfn.name3[i-11];
            }
        }
        cout << endl;
        //cout << name << endl;
    }
    else{
        if(entry->msdos.filename[0] == 0)
            return;
        printf("%x\n",entry->msdos.filename[0]);
    }
    
}
void getName(FatFileLFN* lfn, string* name){
    for(int i=0;i<13;i++){
        if(i<5){
            if(lfn->name1[i] == 0)
                break;
            char a = (char) lfn->name1[i];
            //name.insert(0,&a );
            name->push_back(a);
        }
        else if(i<11){
            if(lfn->name2[i-5] == 0)
                break;
            char a = (char) lfn->name2[i-5];
            //name.insert(0,&a );
            name->push_back(a);
        }
        else{
            if(lfn->name3[i-11] == 0)
                break;
            char a = (char) lfn->name3[i-11];
            //name.insert(0,&a );
            name->push_back(a);
        }
    }   
}


bool cd(string& arg, bool update = true){
    //parse the path
    if(!arg.compare("/")){
        if(update) CurrentDirectory = arg;
        CurrentDirectoryFirstCluster = RootCluster;
        return true;
    }
    else if(!arg.compare("."))
        return true;
    else if(!arg.compare("..")){// go back to parent dir
        if(!CurrentDirectory.compare("/"))
            return true;
        vector<string> path;
        split(CurrentDirectory,path,'/');
        string parentPath = "/";
        if(path[path.size()-2] == ""){
            return cd(parentPath,update);
        }
        else{
            path.pop_back();
            path.erase(path.begin());
            for(auto s:path){
                parentPath += s;
                parentPath += '/';
            }
            parentPath.pop_back();
            //cout << parentPath << endl;
            return cd(parentPath,update);
        }
    }
    vector<string> path;
    split(arg,path,'/');
    uint32_t workingCluster;
    int relative = path[0].compare("");
    if(relative){ // path[0] != ""
        //relative
        workingCluster = CurrentDirectoryFirstCluster;
    }  
    else{
        //absolute
        workingCluster = RootCluster;
        path.erase(path.begin());
    }
    //go to the first cluster of current directory
    lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
    int size = path.size();
    bool found=false;
    
    for(int f=0;f<size;f++){ // relative searching
        if(!path[f].compare("..")){
            found = cd(path[f]);
            workingCluster = CurrentDirectoryFirstCluster;
            continue;
        }
        else if(!path[f].compare(".")){
            found = true;
            continue;
        }
        if(found)
            lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
        else if(f!=0)
            break;
        found = false;

        while(!found){ // traversing the current cluster now
            // look for a file
            int e = 0; // represent how many entry is read in a cluster
            vector<string> names;
            FatFileEntry entry;
            while(1){
                if(e==EntriesPerCluster){
                    e=0;
                    uint32_t fatEntry = getFatEntry(workingCluster);
                    // find next cluster from fatEntry, if there is, update; else, return;
                    if(fatEntry >= 0xffffff8)
                        return false;
                    workingCluster = fatEntry;// update workingCluster
                    lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
                }
                e++;               
                string name;
                read(fd,&entry,sizeof(entry));
                if(entry.lfn.attributes != 0x0F){
                    if(entry.msdos.filename[0] == 0){
                        return false;
                    }
                    else if(entry.msdos.filename[0] == 0x2E || 
                        (entry.msdos.filename[0] == 0x2E && entry.msdos.filename[1] == 0x2E) ||
                        entry.msdos.filename[0] == 0x05 ||
                        entry.msdos.filename[0] == 0xE5){
                            names.clear();
                            continue;
                        }
                        
                    string fullName;
                    for(auto s:names){
                        fullName += s;
                    }
                    //cout << fullName << endl;
                    if(!fullName.compare(path[f]) && entry.msdos.attributes == 0x10){// folder found
                        found = true;
                        //cout << fullName << " is found!" << endl;
                        if(f == size-1){
                            if(update){
                                if(relative){
                                    //append to current one
                                    if(!CurrentDirectory.compare("/")){
                                        for(auto s:path){
                                            if(!s.compare(".."))
                                                continue;
                                            CurrentDirectory+=s;
                                            CurrentDirectory+='/';
                                        }
                                        CurrentDirectory.pop_back();    
                                        //CurrentDirectory.insert(CurrentDirectory.size(), path[f]);
                                    }
                                    else
                                        CurrentDirectory.insert(CurrentDirectory.size(), '/'+path[f]);
                                }
                                else{
                                    //replace with current one
                                    CurrentDirectory = arg;
                                }                                
                            }
                            CurrentDirectoryFirstCluster = entry.msdos.eaIndex | entry.msdos.firstCluster;
                        }
                        else
                            workingCluster = entry.msdos.eaIndex | entry.msdos.firstCluster;
                        break;
                    }
                    else{// check the other files
                        names.clear();
                        continue;
                    }
                }
                else{
                    getName(&entry.lfn,&name);
                    names.insert(names.begin(),name);
                }

            }
        }

    }
    //CurrentDirectory.insert(CurrentDirectory.size() -2, args[1] + '/');
    return found;
}

void ls(bool detail = false){
    uint32_t workingCluster = CurrentDirectoryFirstCluster;
    lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
    vector<string> names;
    int e = 0;
    bool empty = false;
    while(1){
        if(e==EntriesPerCluster){
            e = 0;
            uint32_t fatEntry = getFatEntry(workingCluster);
            // find next cluster, if there is, update; else return;
            if(fatEntry >= 0xffffff8)
                return;
            workingCluster = fatEntry;// update workingCluster
            lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
        }
        e++;
        FatFileEntry entry;
        read(fd,&entry,sizeof(entry));
        if(entry.lfn.attributes == 0){
            if(e==1 || e == 3) empty = true;
            break;
        }
        else if(entry.lfn.attributes != 0x0F){
            if(entry.msdos.filename[0] == 0x2E || 
                (entry.msdos.filename[0] == 0x2E && entry.msdos.filename[1] == 0x2E) ||
                entry.msdos.filename[0] == 0x05 ||
                entry.msdos.filename[0] == 0xE5){
                    names.clear();
                    continue;
            }
            
                
            string fullName;
            for(auto s:names){
                fullName += s;
            }
            if(!detail){
                if(names.size()) cout << fullName << " ";
                names.clear(); 
            }
            else{ // ls -l
                // may be one more cond here like if(names.size())???
                if(entry.msdos.attributes == 0x10){
                    cout << "rwx------ 1 root root ";
                    cout << entry.msdos.fileSize << " ";
                    time_t date = (time_t) entry.msdos.modifiedDate; 
                    tm *datetm = localtime(&date);
                    char datestr[30];
                    strftime(datestr,30,"%b %d",datetm);

                    time_t time = (time_t) entry.msdos.modifiedTime; 
                    tm *timetm = localtime(&time);
                    char timestr[30];
                    strftime(timestr,30,"%R",timetm);                    

                    cout << datestr << " ";
                    cout << timestr << " ";
                    // <file_size_in_bytes> <last_modified_date_and_time> <filename> 
                }
                else if(entry.msdos.attributes == 0x20){
                    cout << "drwx------ 1 root root 0 ";
                    time_t date = (time_t) entry.msdos.modifiedDate; 
                    tm *datetm = localtime(&date);
                    char datestr[30];
                    strftime(datestr,30,"%b %d",datetm);

                    time_t time = (time_t) entry.msdos.modifiedTime; 
                    tm *timetm = localtime(&time);
                    char timestr[30];
                    strftime(timestr,30,"%R",timetm);                    

                    cout << datestr << " ";
                    cout << timestr << " ";
                    // <last_modified_date_and_time>
                }
                if(names.size()) cout << fullName << endl;
                names.clear();                     
            }
        
        }
        else{
            string name;
            getName(&entry.lfn,&name);
            names.insert(names.begin(),name);
        }
    }
    if(!empty && !detail) cout << endl;
    

}


void ls(string& arg,bool detail = false){
    //cout << "arg: " << arg << endl;
    vector<string> path;
    split(arg,path,'/');
    lseek(fd,FirstSectorofCluster(CurrentDirectoryFirstCluster)*BytesPerSector,SEEK_SET);
    string backupCurrentDirectory = CurrentDirectory.substr(0,CurrentDirectory.size());
    //cout << "backup: " << backupCurrentDirectory << endl;
    if(cd(arg,false)){
        ls(detail);
        cd(backupCurrentDirectory,false);
    };

}


int main(){
    string command;
    BPB_struct bpb;
    void* img;
    //FILE *fp = fopen("../../../example.img","w+");
    //if(fp == NULL) cout << "image does not open" << endl;
    fd = open("../../example.img",O_RDONLY);
    if(fd == -1) cout << "image does not open" << endl;

    img = mmap(NULL,512,PROT_READ,MAP_SHARED,fd,0);
    if(img == MAP_FAILED) cout << "map failed" << endl;

    memcpy(&bpb,img,sizeof(BPB_struct));

    ReservedSectorCount = bpb.ReservedSectorCount;
    SectorsPerCluster = bpb.SectorsPerCluster;
    BytesPerSector = bpb.BytesPerSector;
    NumFATs = bpb.NumFATs;
    FATSize = bpb.extended.FATSize;
    RootCluster = bpb.extended.RootCluster;
    FirstDataSector = bpb.ReservedSectorCount + (NumFATs*FATSize);
    EntriesPerCluster = SectorsPerCluster*BytesPerSector/sizeof(FatFileEntry);
    CurrentDirectoryFirstCluster = RootCluster;

    while(1){
        cout << CurrentDirectory + "> ";
        getline(cin,command);
        if(command.compare("quit") == 0){
            break;
        }
        vector<string> args;
        int c = parse(command, args);
        switch (c)
        {
        case 1: // cd
            cd(args[1]);
            break;
        case 2: // ls
            if(args.size() == 1)
                ls();
            else if(args.size() == 2){
                if(!args[1].compare("-l"))
                    ls(true);
                else
                    ls(args[1]);
            }
            else if(args.size() == 3){
                ls(args[2],true);
            }
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