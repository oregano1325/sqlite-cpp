#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>

using namespace std;

// Provided varint parser
int read_varint(ifstream &database_file, int64_t &value)
{
    value = 0;
    int bytes_read = 0;
    char b;
    while (database_file.read(&b, 1))
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
    }
    return bytes_read;
}

vector<string> parse_columns_from_sql(const string &sql_statement)
{
    vector<string> column_names;

    // 1. Find the opening parenthesis containing column definitions
    size_t start = sql_statement.find('(');
    size_t end = sql_statement.find_last_of(')');
    if (start == string::npos || end == string::npos)
        return column_names;

    // Extract everything between the outer parentheses
    string defs = sql_statement.substr(start + 1, end - start - 1);
    stringstream ss(defs);
    string column_def;

    // 2. Split the schema string by commas to isolate each column block
    while (getline(ss, column_def, ','))
    {
        // Trim leading spaces from the definition group
        size_t first_non_space = column_def.find_first_not_of(" \t\n\r");
        if (first_non_space != string::npos)
        {
            column_def = column_def.substr(first_non_space);
        }

        // The column name is the very first word before the data type space
        size_t space_pos = column_def.find_first_of(" \t");
        if (space_pos != string::npos)
        {
            column_names.push_back(column_def.substr(0, space_pos));
        }
        else if (!column_def.empty())
        {
            column_names.push_back(column_def); // fallback if no type is explicitly given
        }
    }
    return column_names;
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

    if (command == ".dbinfo")
    {
        database_file.seekg(103);
        char cntcells[2];
        database_file.read(cntcells, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cntcells[1]) | (static_cast<unsigned char>(cntcells[0]) << 8));
        cout << "database page size: " << page_size << endl;
        cout << "number of tables: " << cell_cnt << endl;
    }
    else if (command == ".tables")
    {
        database_file.seekg(103);
        char cntcells[2];
        database_file.read(cntcells, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cntcells[1]) | (static_cast<unsigned char>(cntcells[0]) << 8));

        database_file.seekg(108);
        vector<unsigned short> cellpos(cell_cnt);
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
    }
    else
    {
        string target_table = "";
        size_t last_space = command.find_last_of(" ");
        if (last_space != string::npos)
        {
            target_table = command.substr(last_space + 1);
        }
        else
        {
            target_table = command;
        }

        database_file.seekg(103);
        char cntcells[2];
        database_file.read(cntcells, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cntcells[1]) | (static_cast<unsigned char>(cntcells[0]) << 8));

        database_file.seekg(108);
        vector<unsigned short> cellpos(cell_cnt);
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

            int64_t type_serial = 0;
            int64_t name_serial = 0;
            int64_t tbl_name_serial = 0;
            int64_t rootpage_serial = 0;
            int64_t sql_serial = 0;

            read_varint(database_file, type_serial);
            read_varint(database_file, name_serial);
            read_varint(database_file, tbl_name_serial);
            read_varint(database_file, rootpage_serial);
            read_varint(database_file, sql_serial);

            int type_len = (type_serial >= 13 && type_serial % 2 != 0) ? (type_serial - 13) / 2 : 0;
            int name_len = (name_serial >= 13 && name_serial % 2 != 0) ? (name_serial - 13) / 2 : 0;
            int tbl_name_len = (tbl_name_serial >= 13 && tbl_name_serial % 2 != 0) ? (tbl_name_serial - 13) / 2 : 0;

            database_file.seekg(st + (streamoff)sz);
            database_file.seekg(type_len, ios::cur);

            char *table_name = new char[name_len + 1];
            database_file.read(table_name, name_len);
            table_name[name_len] = '\0';
            string current_table_name(table_name);
            delete[] table_name;

            if (current_table_name == target_table)
            {
                database_file.seekg(tbl_name_len, ios::cur);
                int64_t target_root_page = 0;

                if (rootpage_serial == 0)
                {
                    target_root_page = 0;
                }
                else if (rootpage_serial == 1)
                {
                    target_root_page = (signed char)database_file.get();
                }
                else if (rootpage_serial == 2)
                {
                    char buf[2];
                    database_file.read(buf, 2);
                    target_root_page = (static_cast<unsigned char>(buf[1]) | (static_cast<unsigned char>(buf[0]) << 8));
                }
                else if (rootpage_serial == 4)
                {
                    char buf[4];
                    database_file.read(buf, 4);
                    target_root_page = (static_cast<unsigned char>(buf[3]) |
                                        (static_cast<unsigned char>(buf[2]) << 8) |
                                        (static_cast<unsigned char>(buf[1]) << 16) |
                                        (static_cast<unsigned char>(buf[0]) << 24));
                }
                else if (rootpage_serial == 8 || rootpage_serial == 9)
                {
                    target_root_page = (rootpage_serial == 9) ? 1 : 0;
                }

                streamoff root_page_offset = (target_root_page - 1) * (streamoff)page_size;
                database_file.seekg(root_page_offset + 3);

                char cnt_buf[2];
                database_file.read(cnt_buf, 2);
                unsigned short row_count = (static_cast<unsigned char>(cnt_buf[1]) | (static_cast<unsigned char>(cnt_buf[0]) << 8));
                cout << row_count << endl;

                // Rest of the data block logic moved here inside the matching target check blocks
                database_file.seekg(sql_serial);
                int sql_len = (sql_serial >= 13 && sql_serial % 2) ? (sql_serial - 13) / 2 : 0;
                string sql_text(sql_len, ' ');
                database_file.read(&sql_text[0], sql_len);
                sql_text[sql_len] = '\0';

                vector<string> columns = parse_columns_from_sql(sql_text);
                string target_column = "name"; // Match actual lowercase string fields
                int target_col_idx = -1;
                for (size_t c = 0; c < columns.size(); c++)
                {
                    if (columns[c] == target_column)
                    {
                        target_col_idx = c;
                        break;
                    }
                }

                streamoff data_page_offset = (target_root_page - 1) * (streamoff)page_size;

                database_file.seekg(data_page_offset + 3);
                char leaf_cnt_buf[2];
                database_file.read(leaf_cnt_buf, 2);
                unsigned short data_cell_cnt = (static_cast<unsigned char>(leaf_cnt_buf[1]) | (static_cast<unsigned char>(leaf_cnt_buf[0]) << 8));

                database_file.seekg(data_page_offset + 8);
                vector<unsigned short> data_cellpos(data_cell_cnt);
                for (int i = 0; i < data_cell_cnt; i++)
                {
                    char ptr[2];
                    database_file.read(ptr, 2);
                    data_cellpos[i] = (static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8));
                }

                for (int i = 0; i < data_cell_cnt; i++)
                {
                    database_file.seekg(data_page_offset + data_cellpos[i]);

                    int64_t payload_size = 0;
                    int64_t row_id = 0;
                    read_varint(database_file, payload_size);
                    read_varint(database_file, row_id);

                    streampos payload_start = database_file.tellg();
                    int64_t header_sz = 0;
                    read_varint(database_file, header_sz);

                    vector<int64_t> serial_types;
                    streampos current_pos = database_file.tellg();

                    while (current_pos < payload_start + (streamoff)header_sz)
                    {
                        int64_t stype = 0;
                        read_varint(database_file, stype);
                        serial_types.push_back(stype);
                        current_pos = database_file.tellg();
                    }

                    database_file.seekg(payload_start + (streamoff)header_sz);

                    for (int col = 0; col < target_col_idx; col++)
                    {
                        int64_t stype = serial_types[col];
                        int len = 0;
                        if (stype >= 13 && stype % 2 != 0)
                            len = (stype - 13) / 2;
                        else if (stype == 1)
                            len = 1;
                        else if (stype == 2)
                            len = 2;
                        else if (stype == 4)
                            len = 4;

                        database_file.seekg(len, ios::cur);
                    }

                    int64_t target_stype = serial_types[target_col_idx];
                    if (target_stype >= 13 && target_stype % 2 != 0)
                    {
                        int string_len = (target_stype - 13) / 2;
                        char *col_val = new char[string_len + 1];
                        database_file.read(col_val, string_len);
                        col_val[string_len] = '\0';

                        cout << col_val << endl;
                        delete[] col_val;
                    }
                }
                break;
            }
        }
    }
    return 0;
}