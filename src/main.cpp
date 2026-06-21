#include <cstring>
#include <iostream>
#include <fstream>
using namespace std;

int read_varint(ifstream &database_file, int64_t &value)
{
    value = 0;
    int bytes_read = 0;
    char b;
    do
    {
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
}

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

        // Safe pointer layout array allocation
        unsigned short cellpos[cell_cnt];
        for (int i = 0; i < cell_cnt; i++)
        {
            char ptr[2];
            database_file.read(ptr, 2);
            cellpos[i] = ((static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8)));
        }

        for (int i = 0; i < cell_cnt; i++)
        {
            database_file.seekg(cellpos[i]);

            int64_t payload_size = 0;
            int64_t row_id = 0;

            read_varint(database_file, payload_size);
            read_varint(database_file, row_id);

            streampos st = database_file.tellg();
            int64_t sz = 0;
            read_varint(database_file, sz);

            int64_t type = 0;
            int64_t name = 0;
            read_varint(database_file, type);
            read_varint(database_file, name);

            int type_len = (type >= 13 && type % 2 != 0) ? (type - 13) / 2 : 0;
            int name_len = (name >= 13 && name % 2 != 0) ? (name - 13) / 2 : 0;

            database_file.seekg(st + (streamoff)sz);
            database_file.seekg(type_len, ios::cur);

            char *table_name = new char[name_len + 1];
            database_file.read(table_name, name_len);
            table_name[name_len] = '\0';
            cout << table_name << " ";
            delete[] table_name;
        }
        cout << endl;
        delete[] cellpos; // Clean up memory
    }
    else
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

        // Safe pointer layout array allocation
        unsigned short cellpos[cell_cnt];
        for (int i = 0; i < cell_cnt; i++)
        {
            char ptr[2];
            database_file.read(ptr, 2);
            cellpos[i] = ((static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8)));
        }
        for (int i = 0; i < cell_cnt; i++)
        {
            database_file.seekg(cellpos[i]);

            int64_t payload_size = 0;
            int64_t row_id = 0;

            read_varint(database_file, payload_size);
            read_varint(database_file, row_id);

            streampos st = database_file.tellg();
            int64_t sz = 0;
            read_varint(database_file, sz); // Header size

            // We MUST read all 4 serial codes from the header sequentially
            int64_t type_serial = 0;
            int64_t name_serial = 0;
            int64_t tbl_name_serial = 0;
            int64_t rootpage_serial = 0;

            read_varint(database_file, type_serial);
            read_varint(database_file, name_serial);
            read_varint(database_file, tbl_name_serial);
            read_varint(database_file, rootpage_serial);

            // Calculate data lengths from serial codes
            int type_len = (type_serial >= 13 && type_serial % 2 != 0) ? (type_serial - 13) / 2 : 0;
            int name_len = (name_serial >= 13 && name_serial % 2 != 0) ? (name_serial - 13) / 2 : 0;
            int tbl_name_len = (tbl_name_serial >= 13 && tbl_name_serial % 2 != 0) ? (tbl_name_serial - 13) / 2 : 0;

            // Jump past the header entirely to enter the Data Body
            database_file.seekg(st + (streamoff)sz);

            // Skip column 0 (type)
            database_file.seekg(type_len, ios::cur);

            // Read column 1 (name)
            char *table_name = new char[name_len + 1];
            database_file.read(table_name, name_len);
            table_name[name_len] = '\0';
            string current_table_name(table_name);
            delete[] table_name;

            // Check if this is the table we are looking for (passed from command line)
            if (current_table_name == target_table)
            {
                // Skip column 2 (tbl_name) to reach column 3 (rootpage)
                database_file.seekg(tbl_name_len, ios::cur);

                int64_t target_root_page = 0;

                // Parse rootpage depending on its integer storage size
                if (rootpage_serial == 1)
                {
                    // 1-byte signed integer
                    target_root_page = (signed char)database_file.get();
                }
                else if (rootpage_serial == 2)
                {
                    // 2-byte big-endian integer
                    char buf[2];
                    database_file.read(buf, 2);
                    target_root_page = (static_cast<unsigned char>(buf[1]) | (static_cast<unsigned char>(buf[0]) << 8));
                }
                else if (rootpage_serial == 4)
                {
                    // 4-byte big-endian integer
                    char buf[4];
                    database_file.read(buf, 4);
                    target_root_page = (static_cast<unsigned char>(buf[3]) |
                                        (static_cast<unsigned char>(buf[2]) << 8) |
                                        (static_cast<unsigned char>(buf[1]) << 16) |
                                        (static_cast<unsigned char>(buf[0]) << 24));
                }

                // --- STEP 4: Jump to that root page and read its row count ---
                streamoff root_page_offset = (target_root_page - 1) * (streamoff)page_size;

                // Seek to offset 3 of our target leaf page to find its cell count
                database_file.seekg(root_page_offset + 3);

                char cnt_buf[2];
                database_file.read(cnt_buf, 2);
                unsigned short row_count = (static_cast<unsigned char>(cnt_buf[1]) | (static_cast<unsigned char>(cnt_buf[0]) << 8));

                // Output exactly what CodeCrafters expects and terminate
                cout << row_count << endl;
                break;
            }
        }
    }

    return 0;
}