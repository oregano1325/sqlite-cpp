#include <cstring>
#include <iostream>
#include <fstream>
using namespace std;

int main(int argc, char *argv[])
{
    cout << unitbuf;
    cerr << unitbuf;

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

        database_file.seekg(16);

        char buffer[2];
        database_file.read(buffer, 2);

        unsigned short page_size = (static_cast<unsigned char>(buffer[1]) | (static_cast<unsigned char>(buffer[0]) << 8));
        database_file.seekg(103);
        char cntcells[2];
        database_file.read(cntcells, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cntcells[1]) | (static_cast<unsigned char>(cntcells[0]) << 8));
        cout << "database page size: " << page_size << endl;
        cout << "number of tables: " << cell_cnt << endl;
    }
    else if (command == ".tables")
    {
        ifstream database_file(database_file_path, ios::binary);
        if (!database_file)
        {
            cerr << "Failed to open the database file" << endl;
            return 1;
        }

        database_file.seekg(103);
        char cntcells[2];
        database_file.read(cntcells, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cntcells[1]) | (static_cast<unsigned char>(cntcells[0]) << 8));

        database_file.seekg(108);
        unsigned short *cellpos = new unsigned short[cell_cnt];
        for (int i = 0; i < cell_cnt; i++)
        {
            char ptr[2];
            database_file.read(ptr, 2);
            cellpos[i] = ((static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8)));
        }

        auto read_varint = [&database_file](int64_t &value) -> int
        {
            value = 0;
            int bytes_read = 0;
            char b;
            do
            {
                database_file.read(&b, 1);
                bytes_read++;
                unsigned char ub = static_cast<unsigned char>(b);
                if (bytes_read == 9)
                {
                    value = (value << 8) | ub;
                    break;
                }
                value = (value << 7) | (ub & 0x7F);
                if (!(ub & 0x80))
                    break;
            } while (true);
            return bytes_read;
        };

        for (int i = 0; i < cell_cnt; i++)
        {
            database_file.seekg(cellpos[i]);

            int64_t payload_size = 0;
            int64_t row_id = 0;

            read_varint(payload_size);
            read_varint(row_id);

            streampos record_header_start = database_file.tellg();
            int64_t header_size = 0;
            read_varint(header_size);

            int64_t type_serial = 0;
            int64_t name_serial = 0;
            read_varint(type_serial);
            read_varint(name_serial);

            int type_len = (type_serial >= 13 && type_serial % 2 != 0) ? (type_serial - 13) / 2 : 0;
            int name_len = (name_serial >= 13 && name_serial % 2 != 0) ? (name_serial - 13) / 2 : 0;

            database_file.seekg(record_header_start + (streamoff)header_size);
            database_file.seekg(type_len, ios::cur);

            char *table_name = new char[name_len + 1];
            database_file.read(table_name, name_len);
            table_name[name_len] = '\0';

            cout << table_name << " ";
            delete[] table_name;
        }
        cout << endl;
        delete[] cellpos;
    }

    return 0;
}