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

using namespace std;

// GLOBALS

string CurrentDirectory = "/";
uint32_t CurrentDirectoryFirstCluster; // root cluster at the beginning
uint32_t ParentDirectoryFirstCluster;
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
uint32_t* FAT;
int fd;

// HELPER FUNCTIONS

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

void retrieveCluster(uint32_t workingCluster){
    lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
}

string getDate(uint16_t date){
    string months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    uint8_t day = (0x001F & date);
    date = date >> 5;
    uint8_t month = (0x000F & date);
    date = date >> 4;
    uint8_t year = (0x007F & date);
    string str;
    if(month == 0 || month > 11 || day > 31){
        str = " - ";
        return str;
    }
    str = months[month-1];
    str += " ";
    str += to_string(day);
    return str;
}
string getTime(uint16_t time){
    uint8_t sec = (0x001F & time);
    time = time >> 5;
    uint8_t min = (0x003F & time);
    time = time >> 6;
    uint8_t hour = (0x001F & time);
    string str = to_string(hour) + ':' + to_string(min);
    return str;

}
uint16_t setDate(){
    std::time_t t = std::time(0);   // get time now
    std::tm* now = std::localtime(&t);
    uint16_t date;
    date = date | (now->tm_year-80);
    date = date << 9;
    date = date | ((now->tm_mon+1) << 5);
    date = date | (now->tm_mday);
    return date;
} 
uint16_t setTime(){
    std::time_t t = std::time(0);   // get time now
    std::tm* now = std::localtime(&t);
    uint16_t time;
    time = time | (now->tm_hour);
    time = time << 11;
    time = time | ((now->tm_min) << 5);
    time = time | ((now->tm_sec));
    return time;
}   

uint8_t getSequence(FatFileLFN *lfn){
    uint8_t seq = lfn->sequence_number;
    seq = seq << 4;
    seq = seq >> 4;
    return seq;
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

void updatePrompt(string& arg){
    vector<string> path;
    split(arg,path,'/');
    if(!path[0].compare("")) // absolute
        CurrentDirectory = arg;
    else{
        if(!CurrentDirectory.compare("/")){
            for(auto s:path){
                if(!s.compare(".."))
                    continue;
                if(!s.compare("."))
                    continue;
                CurrentDirectory+=s;
                CurrentDirectory+='/';
            }
            CurrentDirectory.pop_back();    
            //CurrentDirectory.insert(CurrentDirectory.size(), path[f]);
        }
        else{
            for(auto s:path){
                if(!s.compare("..")){
                    for(int c = CurrentDirectory.size()-1;c>=0;c--){
                        if(CurrentDirectory[c] == '/'){
                            if(CurrentDirectory.size() > 1) 
                                CurrentDirectory.pop_back();
                            break;
                        }
                        CurrentDirectory.pop_back();
                    }
                    continue;
                }
                else if(!s.compare("."))
                    continue;
                else{
                    if(CurrentDirectory.size() == 1)
                        CurrentDirectory+=s;
                    else
                        CurrentDirectory += ('/' + s);
                }
                    
            }
                //CurrentDirectory.insert(CurrentDirectory.size(), '/'+path[f]);
        }
    }
}

unsigned char lfn_checksum(const unsigned char *pFCBName)
{
   int i;
   unsigned char sum = 0;

   for (i = 11; i; i--)
      sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

   return sum;
}

void readFAT(uint32_t* FAT){
    lseek(fd,ReservedSectorCount*BytesPerSector,SEEK_SET);
    for(int f=0;f<(FATSize*BytesPerSector)/sizeof(uint32_t);f++){
        uint32_t entry;
        read(fd,&entry,sizeof(entry));
        FAT[f] = entry;
    }
}

bool locate(string& arg, uint32_t& foundCluster, FatFile83& foundEntry){
        //parse the path
    if(!arg.compare("/")){
        foundCluster = RootCluster;
        return true;
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
    bool folderFound;
    for(int f=0;f<size;f++){
        folderFound = false;
        if(!path[f].compare("..")){
            if(workingCluster == RootCluster){
                return false;
            }
            else{
                lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
                FatFileEntry firstEntry,secondEntry;
                read(fd,&firstEntry,sizeof(FatFileEntry));
                read(fd,&secondEntry,sizeof(FatFileEntry));
                workingCluster = (secondEntry.msdos.eaIndex << 16)| secondEntry.msdos.firstCluster;
                if(workingCluster == 0)
                    workingCluster = RootCluster;
                foundEntry = secondEntry.msdos;
                found = true;
                folderFound = true;
                continue;
            }
        }
        else if(!path[f].compare(".")){
            found = true;
            folderFound = true;
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
                    uint32_t fatEntry = FAT[workingCluster];//getFatEntry(workingCluster);
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
                    string fullName;
                    for(auto s:names){
                        fullName += s;
                    }
                    //cout << fullName << endl;
                    if(!fullName.compare(path[f])){ //&& entry.msdos.attributes == 0x10){// folder found
                        found = true;
                        //cout << fullName << " is found!" << endl;
                        if(f == size-1){
                            folderFound = true;
                            workingCluster = (entry.msdos.eaIndex << 16) | entry.msdos.firstCluster; 
                            foundEntry = entry.msdos;
                        }
                        else
                            workingCluster = (entry.msdos.eaIndex << 16)| entry.msdos.firstCluster;
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
    if(folderFound){
        /*if(update){
            if(relative)
                updatePrompt(path);
            else
                updatePrompt(arg);                          
        }*/
        foundCluster = workingCluster;
    }

    
    //CurrentDirectory.insert(CurrentDirectory.size() -2, args[1] + '/');
    return folderFound;
}

// COMMANDS

void cd(string& arg){
    //parse the path
    uint32_t firstCluster;
    FatFile83 entry;
    if(locate(arg,firstCluster,entry)){
        if(firstCluster == RootCluster){
            CurrentDirectoryFirstCluster = firstCluster;
            updatePrompt(arg);
        }
        else if(entry.attributes == 0x10 &&  entry.filename[0] != 0x05 && entry.filename[0] != 0xE5){
            CurrentDirectoryFirstCluster = firstCluster;
            updatePrompt(arg);
        }
    }
}

void ls(uint32_t& firstCluster, bool detail = false){
    uint32_t workingCluster = firstCluster;
    lseek(fd,FirstSectorofCluster(workingCluster)*BytesPerSector,SEEK_SET);
    vector<string> names;
    int e = 0;
    bool empty = false;
    while(1){
        if(e==EntriesPerCluster){
            e = 0;
            uint32_t fatEntry = FAT[workingCluster];
            // find next cluster, if there is, update; else return;
            if(fatEntry >= 0xffffff8)
                break;
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
                if(entry.msdos.attributes == 0x20){ // file
                    cout << "rwx------ 1 root root ";
                    uint32_t size = entry.msdos.fileSize; 
                    cout << size << " ";
                    string datestr(getDate(entry.msdos.modifiedDate));
                    string timestr(getTime(entry.msdos.modifiedTime));
                    cout << datestr << " ";
                    cout << timestr << " ";
                    // <file_size_in_bytes> <last_modified_date_and_time> <filename> 
                }
                else if(entry.msdos.attributes == 0x10){ // folder
                    cout << "drwx------ 1 root root 0 ";
                    /*time_t date = (time_t) entry.msdos.modifiedDate; 
                    tm *datetm = localtime(&date);
                    char datestr[30];
                    strftime(datestr,30,"%b %d",datetm);

                    time_t time = (time_t) entry.msdos.modifiedTime; 
                    tm *timetm = localtime(&time);
                    char timestr[30];
                    strftime(timestr,30,"%R",timetm); */                   
                    
                    string datestr(getDate(entry.msdos.modifiedDate));
                    string timestr(getTime(entry.msdos.modifiedTime));

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
    uint32_t firstCluster;
    FatFile83 entry;
    if(locate(arg,firstCluster,entry)){
        if(firstCluster == RootCluster)
            ls(RootCluster, detail);
        else if(entry.attributes == 0x10 && entry.filename[0] != 0x05 && entry.filename[0] != 0xE5)
            ls(firstCluster, detail);
    }
}

void mkdir(string& arg, bool folder = true){ // there are lots of inconsistentcies, review
    // folder = true ---->>> mkdir
    // folder = false ---->>>> touch
    // path->>> folder1/folder2/folder3 case1
    // path->>> /home/pictures/folde2 case2
    vector<string> path;
    split(arg,path,'/');
    string newFolder;
    string parentPath;
    // construct parent path in parentPath
    if(!path[0].compare("")){ //case2
        // absolute
        newFolder = path.back();
        path.pop_back();
        parentPath = "/";
        for(auto s:path){
            if(!s.compare(""))
                continue;
            else
                parentPath+=s+'/';
        }
        parentPath.pop_back();
    }
    else{ //case1
        newFolder = path.back();
        path.pop_back();
        int size = path.size();
        if(CurrentDirectory.size() == 1)
            parentPath=CurrentDirectory;
        else
            parentPath = CurrentDirectory+ '/';
        for(int f=0;f<size-1;f++)
            parentPath+=path[f] + '/';
        if(size) parentPath+=path.back();
    }

    int length = newFolder.size();
    int numOfEntries;
    if(length>13)
        numOfEntries = (length / 13) + 2;
    else
        numOfEntries = 2; // one LFN and one base entry
    vector<FatFileLFN> lfns;
    int offset=0;
    for(int l=1;l<numOfEntries;l++){
        string lfnname = newFolder.substr(offset,offset+13);
        offset+=13;
        FatFileLFN lfn;
        lfn.sequence_number = l;
        int o = 0;
        for(;o<lfnname.size();o++){
            if(o<5){
                lfn.name1[o] = lfnname[o];
            }
            else if(o<11){
                lfn.name2[o-5] = lfnname[o];
            }
            else{
                lfn.name3[o-11] = lfnname[o];
            }
        }
        for(;o<13;o++){
            if(o<5){
                lfn.name1[o] = 0;
            }
            else if(o<11){
                lfn.name2[o-5] = 0;
            }
            else{
                lfn.name3[o-11] = 0;
            }
        }                
        lfn.attributes = 0x0F;
        lfn.reserved = 0x00;
        // calculate the checksum
        lfn.firstCluster = 0x0000;
        lfns.push_back(lfn);
    }

    uint32_t firstCluster; // first cluster of parent folder
    uint32_t backup;
    FatFile83 parentEntry; // msdos entry of the parent folder

    if(!locate(parentPath,firstCluster,parentEntry))
        return;
    if(firstCluster != RootCluster && parentEntry.attributes == 0x20 || parentEntry.filename[0]== 0x05 || parentEntry.filename[0] == 0xE5) // parent folder is not root and parent folder is a file or deleted folder
        return;
    if(firstCluster == RootCluster){
        parentEntry.filename[0] = '.';
        parentEntry.filename[1] = '.';
        parentEntry.filename[2] = ' ';
        parentEntry.filename[3] = ' ';
        parentEntry.filename[4] = ' ';
        parentEntry.filename[5] = ' ';
        parentEntry.filename[6] = ' ';
        parentEntry.filename[7] = ' ';
        parentEntry.extension[0] = ' ';
        parentEntry.extension[1] = ' ';
        parentEntry.extension[2] = ' ';
        parentEntry.attributes = 0x10;
        parentEntry.reserved = 0;
        parentEntry.creationTimeMs = 0;
        parentEntry.creationTime = 37014;
        parentEntry.creationDate = 37014;
        parentEntry.lastAccessTime = 21683;
        parentEntry.eaIndex = 0;
        parentEntry.modifiedTime = 37014;
        parentEntry.modifiedDate = 37014;
        parentEntry.firstCluster = 0;
        parentEntry.fileSize = 0;
    }
    else{
        parentEntry.filename[0] = '.';
        parentEntry.filename[1] = '.';
        parentEntry.filename[2] = ' ';
        parentEntry.filename[3] = ' ';
        parentEntry.filename[4] = ' ';
        parentEntry.filename[5] = ' ';
        parentEntry.filename[6] = ' ';
        parentEntry.filename[7] = ' ';
        parentEntry.extension[0] = ' ';
        parentEntry.extension[1] = ' ';
        parentEntry.extension[2] = ' ';        
    }
    backup = firstCluster;
     // search the cluster, we need numOfEntries space
    int space = 0;
    int filenum = 0;
    retrieveCluster(firstCluster); // lseek
    for(int e = 0;e<=EntriesPerCluster;e++){
        if(e == EntriesPerCluster){ // go to next cluster
            uint32_t fatEntry = FAT[firstCluster]; //getFatEntry(firstCluster);
            if(fatEntry>=0xffffff8){
                // no next cluster
                // allocate new cluster for parent
                uint32_t newParentCluster;
                for(uint32_t e=0;e<(FATSize*BytesPerSector)/sizeof(uint32_t);e++){
                    if(FAT[e] == 0x0000000){
                        newParentCluster = e;
                        break;
                    }
                }                
                // update FAT for parent cluster
                FAT[firstCluster] = newParentCluster;

                FAT[newParentCluster] = 0xffffff8; // eoc

                // find another cluster for the new folder
                uint32_t newCluster;
                for(uint32_t e=0;e<(FATSize*BytesPerSector)/sizeof(uint32_t);e++){
                    if(FAT[e] == 0x0000000){
                        newCluster = e;
                        break;
                    }
                } 

                // update FAT in memory
                FAT[newCluster] = 0xffffff8; //eoc

                // write lfns and msdos
                retrieveCluster(newParentCluster);
                FatFile83 msdos;
                msdos.filename[0] = '~';
                msdos.filename[1] = 48+filenum+1;
                for(int i=2;i<8;i++)
                    msdos.filename[i] = ' ';
                msdos.extension[0] = ' ';
                msdos.extension[1] = ' ';
                msdos.extension[2] = ' ';
                if(folder) msdos.attributes = 0x10;
                else msdos.attributes = 0x20;
                msdos.creationTime = setTime();
                msdos.creationTime = setDate();
                msdos.modifiedTime = setTime();
                msdos.modifiedDate = setDate();
                msdos.firstCluster = 0x0000FFFF & newCluster;
                msdos.eaIndex = (newCluster >> 16) & 0x0000FFFF;
                msdos.fileSize = 0;
                // write lfns and msdos to parent directory's cluster
                for(int i=lfns.size()-1;i>=0;i--){
                    lfns[i].checksum = lfn_checksum(msdos.filename);
                    write(fd,&(lfns[i]),sizeof(FatFileLFN));
                }
                write(fd,&msdos,sizeof(FatFile83));

                // prepare the new cluster
                if(folder){
                    retrieveCluster(newCluster);
                    FatFile83 firstEntry = msdos;
                    FatFile83 secondEntry;
                    firstEntry.filename[0]='.';
                    for(int i=1;i<8;i++)
                        firstEntry.filename[i]=' ';
                    for(int i=0;i<3;i++)
                        firstEntry.extension[i]=' ';
                    /*firstEntry.attributes = 0x10;
                    firstEntry.reserved = msdos.reserved;
                    firstEntry.creationTime = msdos.creationTime;
                    firstEntry.creationDate = msdos.creationDate;
                    firstEntry.modifiedTime = msdos.modifiedTime;
                    firstEntry.modifiedDate = msdos.modifiedDate;
                    firstEntry.eaIndex = msdos.eaIndex;
                    firstEntry.firstCluster = msdos.firstCluster;
                    firstEntry.fileSize = 0;*/

                    /*secondEntry = parentEntry;
                    secondEntry.filename[0]='.';
                    secondEntry.filename[1]='.';
                    for(int i=2;i<8;i++)
                        secondEntry.filename[i]=' ';
                    for(int i=0;i<3;i++)
                        secondEntry.extension[i]=' ';
                    secondEntry.attributes = 0x10;
                    //secondEntry.reserved = parentEntry.reserved;
                    secondEntry.firstCluster = 0x0000FFFF & backup;
                    secondEntry.eaIndex = (backup >> 16) & 0xFFFF;
                    secondEntry.fileSize = 0;            
                    write(fd,&firstEntry,sizeof(FatFile83));
                    write(fd,&secondEntry,sizeof(FatFile83));*/
                    write(fd,&firstEntry,sizeof(FatFile83));
                    write(fd,&parentEntry,sizeof(FatFile83));                    
                }
                return;
            }
            firstCluster = fatEntry;
            retrieveCluster(firstCluster);
            e=0;
            continue;
        }
        FatFileEntry entry;
        read(fd,&entry,sizeof(FatFileEntry));
        if(entry.msdos.filename[0] == 0x2E) // . & ..
            continue;
        if(entry.msdos.attributes == 0x10 || entry.msdos.attributes == 0x20)
            filenum++;
        if(entry.msdos.filename[0] == 0x00 || entry.msdos.filename[0] == 0x05 || entry.msdos.filename[0] == 0xE5) // find available entries
            space++;
        else
            space = 0;
        if(space == numOfEntries){// found enough space
            
            // allocate new cluster
            uint32_t newCluster;
            for(uint32_t e=0;e<(FATSize*BytesPerSector)/sizeof(uint32_t);e++){
                if(FAT[e] == 0x0000000){
                    newCluster = e;
                    break;
                }
            }

            // update FAT in memory
            FAT[newCluster] = 0xffffff8; // eoc

            lseek(fd,(-numOfEntries)*sizeof(FatFileEntry),SEEK_CUR);
            FatFile83 msdos; // create base entry
            msdos.filename[0] = '~';
            msdos.filename[1] = 48+filenum+1;
            for(int i=2;i<8;i++)
                msdos.filename[i] = ' ';
            msdos.extension[0] = ' ';
            msdos.extension[1] = ' ';
            msdos.extension[2] = ' ';
            if(folder) msdos.attributes = 0x10;
            else msdos.attributes = 0x20;
            msdos.creationTime = setTime();
            msdos.creationTime = setDate();
            msdos.modifiedTime = setTime();
            msdos.modifiedDate = setDate();
            msdos.firstCluster = 0x0000FFFF & newCluster;
            msdos.eaIndex = (newCluster >> 16) & 0xFFFF;
            msdos.fileSize = 0;
            // write lfns and msdos to parent directory's cluster
            for(int i=lfns.size()-1;i>=0;i--){
                lfns[i].checksum = lfn_checksum(msdos.filename);
                write(fd,&(lfns[i]),sizeof(FatFileLFN));
            }
                
            write(fd,&msdos,sizeof(FatFile83));            

            // prepare the new cluster for newly created folder
            if(folder){
                retrieveCluster(newCluster);
                FatFile83 firstEntry = msdos;
                FatFile83 secondEntry;
                firstEntry.filename[0]='.';
                for(int i=1;i<8;i++)
                    firstEntry.filename[i]=' ';
                for(int i=0;i<3;i++)
                    firstEntry.extension[i]=' ';
                /*firstEntry.attributes = 0x10;
                firstEntry.reserved = msdos.reserved;
                firstEntry.creationTime = msdos.creationTime;
                firstEntry.creationDate = msdos.creationDate;
                firstEntry.modifiedTime = msdos.modifiedTime;
                firstEntry.modifiedDate = msdos.modifiedDate;
                firstEntry.eaIndex = msdos.eaIndex;
                firstEntry.firstCluster = msdos.firstCluster;
                firstEntry.fileSize = 0;*/

                /*secondEntry = parentEntry;
                secondEntry.filename[0]='.';
                secondEntry.filename[1]='.';
                for(int i=2;i<8;i++)
                    secondEntry.filename[i]=' ';
                for(int i=0;i<3;i++)
                    secondEntry.extension[i]=' ';
                secondEntry.attributes = 0x10;
                //secondEntry.reserved = parentEntry.reserved;
                secondEntry.firstCluster = 0x0000FFFF & backup;
                secondEntry.eaIndex = (backup >> 16) & 0xFFFF;
                secondEntry.fileSize = 0;            
                write(fd,&firstEntry,sizeof(FatFile83));
                write(fd,&secondEntry,sizeof(FatFile83));*/
                write(fd,&firstEntry,sizeof(FatFile83));
                write(fd,&parentEntry,sizeof(FatFile83));
            }
            return;
        }
    }

}

void touch(string& arg){
    mkdir(arg, false);
}

void cat(string& arg){
    uint32_t firstCluster;
    FatFile83 entry;
    if(!locate(arg,firstCluster,entry)){
        return;
    }
    if(entry.attributes == 0x10) // its a folder
        return;
    if(entry.filename[0] == 0x05 || entry.filename[0] == 0xE5)
        return;
    char ch = 1;
    int r=0;
    retrieveCluster(firstCluster);
    while(ch != 0){
        if(r==(EntriesPerCluster*sizeof(FatFileEntry))){
            // go other cluster
            uint32_t fatEntry = FAT[firstCluster]; //getFatEntry(firstCluster);
            if(fatEntry >= 0xFFFFFF8){
                break;
            }
            firstCluster = fatEntry;
            retrieveCluster(firstCluster);
            r=0;
            continue;
        }
        read(fd,&ch,sizeof(ch));
        cout << ch;
        r++;
    }
    cout << endl;
}

void mv(string& src, string& dest){
    uint32_t firstCluster;
    uint32_t destFirstCluster;
    FatFile83 msdos; // file83 entry of original file or folder
    FatFile83 destMsdos; // file83 entry of original file or folder
    if(!locate(src,firstCluster,msdos) || !locate(dest,destFirstCluster,destMsdos))
        return;
    vector<string> path;
    split(src,path,'/');
    string original;
    string parentPath;
    // construct parent path in parentPath
    if(!path[0].compare("")){ //case2
        // absolute
        original = path.back();
        path.pop_back();
        parentPath = "/";
        for(auto s:path){
            if(!s.compare(""))
                continue;
            else
                parentPath+=s+'/';
        }
        parentPath.pop_back();
    }
    else{ //case1
        original = path.back();
        path.pop_back();
        int size = path.size();
        if(CurrentDirectory.size() == 1)
            parentPath=CurrentDirectory;
        else
            parentPath = CurrentDirectory+ '/';
        for(int f=0;f<size-1;f++)
            parentPath+=path[f] + '/';
        if(size) parentPath+=path.back();
    }
    locate(parentPath,firstCluster,msdos);
    uint32_t workingCluster = firstCluster;
    int e = 0; // represent how many entry is read in a cluster
    vector<string> names;
    vector<FatFileEntry> originalEntries;
    FatFileEntry entry;
    retrieveCluster(workingCluster);
    while(1){
        if(e==EntriesPerCluster){
            e=0;
            uint32_t fatEntry = FAT[workingCluster];//getFatEntry(workingCluster);
            // find next cluster from fatEntry, if there is, update; else, return;
            if(fatEntry >= 0xffffff8)
                return;
            workingCluster = fatEntry;// update workingCluster
            retrieveCluster(workingCluster);
        }
        e++;               
        string name;
        read(fd,&entry,sizeof(entry));
        originalEntries.push_back(entry);
        if(entry.lfn.attributes != 0x0F){
            if(entry.msdos.filename[0] == 0){
                return;
            }
            string fullName;
            for(auto s:names){
                fullName += s;
            }
            //cout << fullName << endl;
            if(!fullName.compare(original)){ //&& entry.msdos.attributes == 0x10){// folder found
                //cout << fullName << " is found!" << endl;
                lseek(fd,-1*(sizeof(FatFileEntry)),SEEK_CUR);// mark as deleted
                uint8_t deleted = 0xE5;
                write(fd,&deleted,sizeof(uint8_t));
                break;
            }
            else{// check the other files
                names.clear();
                originalEntries.clear();
                continue;
            }
        }
        else{
            getName(&entry.lfn,&name);
            names.insert(names.begin(),name);
        }
    }
    // we have original entries here
    workingCluster = destFirstCluster;
    int numOfEntries = originalEntries.size();
    int space = 0;
    int filenum = 0;    
    retrieveCluster(workingCluster);
    for(int e = 0;e<=EntriesPerCluster;e++){
        if(e == EntriesPerCluster){ // go to next cluster
            uint32_t fatEntry = FAT[firstCluster]; //getFatEntry(firstCluster);
            if(fatEntry>=0xffffff8){
                // no next cluster
                // allocate new cluster for parent
                uint32_t newParentCluster;
                for(uint32_t e=0;e<(FATSize*BytesPerSector)/sizeof(uint32_t);e++){
                    if(FAT[e] == 0x0000000){
                        newParentCluster = e;
                        break;
                    }
                }                
                // update FAT for parent cluster
                FAT[firstCluster] = newParentCluster;

                FAT[newParentCluster] = 0xffffff8; // eoc

                // write lfns and msdos
                retrieveCluster(newParentCluster);
                FatFileEntry& originalMsdos = originalEntries.back(); // create base entry

                // write lfns and msdos to parent directory's cluster
                for(int i=0;i<numOfEntries-1;i++){
                    originalEntries[i].lfn.checksum = lfn_checksum(originalMsdos.msdos.filename);
                    write(fd,&(originalEntries[i]),sizeof(FatFileEntry));
                } 
                write(fd,&originalEntries.back().msdos,sizeof(FatFile83));            

                // prepare the cluster for new parent folder
                if(originalEntries.back().msdos.attributes == 0x10){ // folder
                    workingCluster =  (originalEntries.back().msdos.eaIndex << 16) | originalEntries.back().msdos.firstCluster;
                    retrieveCluster(workingCluster);
                    FatFileEntry firstEntry,secondEntry;
                    read(fd,&firstEntry,sizeof(FatFileEntry));
                    read(fd,&secondEntry,sizeof(FatFileEntry));

                    secondEntry.msdos.firstCluster = 0x0000FFFF & destFirstCluster;
                    secondEntry.msdos.eaIndex = (destFirstCluster >> 16) & 0xFFFF;
            
                    lseek(fd,-1*sizeof(FatFileEntry),SEEK_CUR);
                    write(fd,&secondEntry,sizeof(FatFile83));
                }
                return;
            }
            workingCluster = fatEntry;
            retrieveCluster(workingCluster);
            e=0;
            continue;
        }
        FatFileEntry entry;
        read(fd,&entry,sizeof(FatFileEntry));
        if(entry.msdos.filename[0] == 0x2E) // . & ..
            continue;
        if(entry.msdos.attributes == 0x10 || entry.msdos.attributes == 0x20)
            filenum++;
        if(entry.msdos.filename[0] == 0x00 || entry.msdos.filename[0] == 0x05 || entry.msdos.filename[0] == 0xE5) // find available entries
            space++;
        else
            space = 0;
        if(space == numOfEntries){// found enough space
            
            // allocate new cluster

            lseek(fd,(-numOfEntries)*sizeof(FatFileEntry),SEEK_CUR);
            FatFileEntry& originalMsdos = originalEntries.back(); // create base entry

            // write lfns and msdos to parent directory's cluster
            for(int i=0;i<numOfEntries-1;i++){
                originalEntries[i].lfn.checksum = lfn_checksum(originalMsdos.msdos.filename);
                write(fd,&(originalEntries[i]),sizeof(FatFileEntry));
            } 
            write(fd,&originalMsdos.msdos,sizeof(FatFile83));            

            // prepare the cluster for new parent folder
            if(originalMsdos.msdos.attributes == 0x10){ // folder
                workingCluster =  (originalMsdos.msdos.eaIndex << 16) | originalMsdos.msdos.firstCluster;
                retrieveCluster(workingCluster);
                FatFileEntry firstEntry,secondEntry;
                read(fd,&firstEntry,sizeof(FatFileEntry));
                read(fd,&secondEntry,sizeof(FatFileEntry));

                secondEntry.msdos.firstCluster = 0x0000FFFF & destFirstCluster;
                secondEntry.msdos.eaIndex = (destFirstCluster >> 16) & 0xFFFF;
          
                lseek(fd,-1*sizeof(FatFileEntry),SEEK_CUR);
                write(fd,&secondEntry,sizeof(FatFile83));
            }
            return;
        }
    }    
}


int main(){
    string command;
    BPB_struct bpb;
    void* img;
    //FILE *fp = fopen("../../../example.img","w+");
    //if(fp == NULL) cout << "image does not open" << endl;
    fd = open("../../example.img",O_RDWR);
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
    FAT = new uint32_t[(FATSize*BytesPerSector)/4];
    readFAT(FAT);
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
                ls(CurrentDirectoryFirstCluster);
            else if(args.size() == 2){
                if(!args[1].compare("-l"))
                    ls(CurrentDirectoryFirstCluster,true);
                else
                    ls(args[1]);
            }
            else if(args.size() == 3){
                ls(args[2],true);
            }
            break;
        case 3: // mkdir
            mkdir(args[1]);
            break;
        case 4: // touch
            touch(args[1]);
            break;
        case 5: // mv
            mv(args[1],args[2]);
            break;
        case 6: // cat
            cat(args[1]);
            break;
        }
    }
    getFatEntry(0);
    for(uint32_t e=0;e<(FATSize*BytesPerSector)/sizeof(uint32_t);e++)
        write(fd,&FAT[e],sizeof(uint32_t));
    close(fd);
    return 0;
}