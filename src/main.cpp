#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace std;

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

    size_t start = sql_statement.find('(');
    size_t end = sql_statement.find_last_of(')');
    if (start == string::npos or end == string::npos)
        return column_names;

    string defs = sql_statement.substr(start + 1, end - start - 1);
    stringstream ss(defs);
    string column_def;

    while (getline(ss, column_def, ','))
    {
        size_t first_non_space = column_def.find_first_not_of(" \t\n\r");
        if (first_non_space != string::npos)
        {
            column_def = column_def.substr(first_non_space);
        }

        size_t space_pos = column_def.find_first_of(" \t");
        if (space_pos != string::npos)
        {
            column_names.push_back(column_def.substr(0, space_pos));
        }
        else if (!column_def.empty())
        {
            column_names.push_back(column_def);
        }
    }
    return column_names;
}

// Helper tool to trim spaces
string trim(const string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
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

            int type_len = (type >= 13 and type % 2 != 0) ? (type - 13) / 2 : 0;
            int name_len = (name >= 13 and name % 2 != 0) ? (name - 13) / 2 : 0;

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
        // Parse table name out of command
        string target_table = "";
        size_t last_space = command.find_last_of(" ");
        if (last_space != string::npos)
            target_table = command.substr(last_space + 1);
        else
            target_table = command;

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

            int type_len = (type_serial >= 13 and type_serial % 2 != 0) ? (type_serial - 13) / 2 : 0;
            int name_len = (name_serial >= 13 and name_serial % 2 != 0) ? (name_serial - 13) / 2 : 0;
            int tbl_name_len = (tbl_name_serial >= 13 and tbl_name_serial % 2 != 0) ? (tbl_name_serial - 13) / 2 : 0;

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
                    rootpage_serial = 0;
                else if (rootpage_serial == 1)
                    target_root_page = (signed char)database_file.get();
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

                database_file.seekg(st + (streamoff)sz);
                database_file.seekg(type_len + name_len + tbl_name_len, ios::cur);

                int root_len = (rootpage_serial <= 4) ? rootpage_serial : 0;
                database_file.seekg(root_len, ios::cur);

                int sql_len = (sql_serial >= 13 and sql_serial % 2) ? (sql_serial - 13) / 2 : 0;
                string sql_text(sql_len, ' ');
                database_file.read(&sql_text[0], sql_len);

                vector<string> table_columns = parse_columns_from_sql(sql_text);

                // Parse out multiple columns from SELECT statement
                vector<string> target_columns;
                bool is_count_query = false;

                if (command.rfind("SELECT ", 0) == 0 || command.rfind("select ", 0) == 0)
                {
                    size_t select_pos = 7; // Length of "SELECT "
                    size_t from_pos = command.find(" FROM");
                    if (from_pos == string::npos)
                        from_pos = command.find(" from");

                    if (from_pos != string::npos)
                    {
                        string col_part = command.substr(select_pos, from_pos - select_pos);
                        stringstream col_ss(col_part);
                        string col_name;
                        while (getline(col_ss, col_name, ','))
                        {
                            string cleaned = trim(col_name);
                            string lower_cleaned = cleaned;
                            transform(lower_cleaned.begin(), lower_cleaned.end(), lower_cleaned.begin(), ::tolower);

                            if (lower_cleaned == "count(*)")
                            {
                                is_count_query = true;
                                break;
                            }
                            target_columns.push_back(lower_cleaned);
                        }
                    }
                }

                // Map requested columns to schema indexes
                vector<int> target_col_indices;
                if (!is_count_query)
                {
                    for (const auto &t_col : target_columns)
                    {
                        int idx = -1;
                        for (size_t c = 0; c < table_columns.size(); c++)
                        {
                            string col_lower = table_columns[c];
                            transform(col_lower.begin(), col_lower.end(), col_lower.begin(), ::tolower);
                            if (col_lower == t_col)
                            {
                                idx = c;
                                break;
                            }
                        }
                        target_col_indices.push_back(idx); // Will contain -1 if not found
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

                int64_t total_rows_counted = 0;

                for (int i = 0; i < data_cell_cnt; i++)
                {
                    database_file.seekg(data_page_offset + data_cellpos[i]);

                    int64_t payload_size = 0;
                    int64_t row_id = 0;
                    read_varint(database_file, payload_size);
                    read_varint(database_file, row_id);

                    if (is_count_query)
                    {
                        total_rows_counted++;
                        continue;
                    }

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

                    // Read sequential record data values into memory structure
                    vector<string> row_values(serial_types.size(), "");
                    for (size_t col = 0; col < serial_types.size(); col++)
                    {
                        int64_t stype = serial_types[col];
                        int data_len = 0;
                        if (stype >= 13 and stype % 2 != 0)
                            data_len = (stype - 13) / 2;
                        else if (stype == 1)
                            data_len = 1;
                        else if (stype == 2)
                            data_len = 2;
                        else if (stype == 4)
                            data_len = 4;

                        if (data_len > 0)
                        {
                            if (stype >= 13 and stype % 2 != 0)
                            {
                                char *col_val = new char[data_len + 1];
                                database_file.read(col_val, data_len);
                                col_val[data_len] = '\0';
                                row_values[col] = string(col_val);
                                delete[] col_val;
                            }
                            else
                            {
                                // For basic integers (like ID types), quickly skip or handle
                                database_file.seekg(data_len, ios::cur);
                                row_values[col] = "[IntData]";
                            }
                        }
                    }

                    // Print requested target columns separated by '|'
                    string output_line = "";
                    for (size_t k = 0; k < target_col_indices.size(); k++)
                    {
                        int mapped_idx = target_col_indices[k];
                        if (mapped_idx != -1 && mapped_idx < (int)row_values.size())
                        {
                            output_line += row_values[mapped_idx];
                        }
                        if (k < target_col_indices.size() - 1)
                        {
                            output_line += "|";
                        }
                        // cout << "\n";
                    }
                    cout << output_line << endl;
                }

                if (is_count_query)
                {
                    cout << total_rows_counted << endl;
                }
                break;
            }
        }
    }
    return 0;
}