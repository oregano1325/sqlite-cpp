#include <cstring>
#include <iostream>
#include <fstream>
using namespace std;
int main(int argc, char *argv[])
{
    // Flush after every cout / cerr
    cout << unitbuf;
    cerr << unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    cerr << "Logs from your program will appear here" << endl;

    if (argc != 3)
    {
        cerr << "Expected two arguments" << endl;
        return 1;
    }

    string database_file_path = argv[1];
    string command = argv[2];

    if (command == ".dbinfo")
    {
        ifstream database_file(database_file_path, ios::binary);
        if (!database_file)
        {
            cerr << "Failed to open the database file" << endl;
            return 1;
        }

        database_file.seekg(16); // Skip the first 16 bytes of the header

        char buffer[2];
        database_file.read(buffer, 2);

        unsigned short page_size = (static_cast<unsigned char>(buffer[1]) | (static_cast<unsigned char>(buffer[0]) << 8));
        database_file.seekg(103); // skipping 100 bytes of header(since page1) and number of cells at offset 3
        char cntcells[2];
        database_file.read(cntcells, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cntcells[1]) | (static_cast<unsigned char>(cntcells[1]) << 8));
        cout << "database page size: " << page_size << endl;
        cout << "number of tables: " << cell_cnt << endl;
    }

    return 0;
}
