#include "fat32.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

string CurrentDirectory = "/> ";

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

void cd(vector<string>& args){
    //CurrentDirectory.insert(CurrentDirectory.size() -2, args[1] + '/');
}

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

int main(){
    string command;
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
    return 0;
}