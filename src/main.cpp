#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace std;

// read variable length integers
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

// grab column names from the create table string
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
            column_def = column_def.substr(first_non_space);

        size_t space_pos = column_def.find_first_of(" \t");
        if (space_pos != string::npos)
            column_names.push_back(column_def.substr(0, space_pos));
        else if (!column_def.empty())
            column_names.push_back(column_def);
    }
    return column_names;
}

// helper to strip whitespace
string trim(const string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// --- NEW MAGIC: RECURSIVE B-TREE TRAVERSAL ---
// Give this function a page number, and it will dig down to the bottom of the
// tree and return a list of all the pages that ACTUALLY contain data.
vector<int> get_leaf_pages(ifstream &db, int page_num, int page_size)
{
    vector<int> leaves;
    streamoff offset = (page_num - 1) * (streamoff)page_size;

    // Check what kind of page we are standing on
    db.seekg(offset);
    char type_flag;
    db.read(&type_flag, 1);

    if (type_flag == 0x0D)
    {
        // BASE CASE: It's a leaf page! We hit the jackpot.
        // Just return this page number, no need to dig deeper.
        leaves.push_back(page_num);
    }
    else if (type_flag == 0x05)
    {
        // RECURSIVE CASE: It's an interior directory page.
        db.seekg(offset + 3);
        char cnt_buf[2];
        db.read(cnt_buf, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cnt_buf[1]) | (static_cast<unsigned char>(cnt_buf[0]) << 8));

        // Grab the Right-Most Pointer from the header
        db.seekg(offset + 8);
        char right_buf[4];
        db.read(right_buf, 4);
        int right_page = (static_cast<unsigned char>(right_buf[3]) |
                          (static_cast<unsigned char>(right_buf[2]) << 8) |
                          (static_cast<unsigned char>(right_buf[1]) << 16) |
                          (static_cast<unsigned char>(right_buf[0]) << 24));

        // Read the cell pointers
        db.seekg(offset + 12); // Notice interior headers are 12 bytes, not 8!
        vector<unsigned short> cellpos(cell_cnt);
        for (int i = 0; i < cell_cnt; i++)
        {
            char ptr[2];
            db.read(ptr, 2);
            cellpos[i] = (static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8));
        }

        // Loop through all cells to find the Left Pointers
        for (int i = 0; i < cell_cnt; i++)
        {
            db.seekg(offset + cellpos[i]);

            // In an interior cell, the first 4 bytes are the pointer to the left child page
            char left_buf[4];
            db.read(left_buf, 4);
            int left_page = (static_cast<unsigned char>(left_buf[3]) |
                             (static_cast<unsigned char>(left_buf[2]) << 8) |
                             (static_cast<unsigned char>(left_buf[1]) << 16) |
                             (static_cast<unsigned char>(left_buf[0]) << 24));

            // Recursively dig into the left page and merge its results
            vector<int> child_leaves = get_leaf_pages(db, left_page, page_size);
            leaves.insert(leaves.end(), child_leaves.begin(), child_leaves.end());
        }

        // Finally, dig into the right-most page and merge its results
        vector<int> right_leaves = get_leaf_pages(db, right_page, page_size);
        leaves.insert(leaves.end(), right_leaves.begin(), right_leaves.end());
    }

    return leaves;
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

    // grab page size
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
            int64_t payload_size = 0, row_id = 0, sz = 0;
            read_varint(database_file, payload_size);
            read_varint(database_file, row_id);

            streampos st = database_file.tellg();
            read_varint(database_file, sz);

            int64_t type = 0, name = 0;
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
        // parse query string
        string target_table = "";
        string where_col = "";
        string where_val = "";
        bool has_where = false;

        string cmd_lower = command;
        transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);

        size_t from_pos = cmd_lower.find(" from ");
        size_t where_pos = cmd_lower.find(" where ");

        if (from_pos != string::npos)
        {
            if (where_pos != string::npos)
            {
                target_table = trim(command.substr(from_pos + 6, where_pos - (from_pos + 6)));
                has_where = true;

                string condition = trim(command.substr(where_pos + 7));
                size_t eq_pos = condition.find('=');
                if (eq_pos != string::npos)
                {
                    where_col = trim(condition.substr(0, eq_pos));
                    where_val = trim(condition.substr(eq_pos + 1));

                    if (where_val.length() >= 2 && where_val.front() == '\'' && where_val.back() == '\'')
                    {
                        where_val = where_val.substr(1, where_val.length() - 2);
                    }
                }
            }
            else
            {
                target_table = trim(command.substr(from_pos + 6));
            }
        }
        else
        {
            target_table = command;
            size_t last_space = target_table.find_last_of(" ");
            if (last_space != string::npos)
                target_table = target_table.substr(last_space + 1);
        }

        // read page 1 to find root page
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
            int64_t payload_size = 0, row_id = 0, sz = 0;
            read_varint(database_file, payload_size);
            read_varint(database_file, row_id);

            streampos st = database_file.tellg();
            read_varint(database_file, sz);

            int64_t type_serial = 0, name_serial = 0, tbl_name_serial = 0, rootpage_serial = 0, sql_serial = 0;
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

            // Found our table schema!
            if (current_table_name == target_table)
            {
                database_file.seekg(tbl_name_len, ios::cur);
                int64_t target_root_page = 0;

                if (rootpage_serial == 0)
                    target_root_page = 0;
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
                    target_root_page = (static_cast<unsigned char>(buf[3]) | (static_cast<unsigned char>(buf[2]) << 8) |
                                        (static_cast<unsigned char>(buf[1]) << 16) | (static_cast<unsigned char>(buf[0]) << 24));
                }

                database_file.seekg(st + (streamoff)sz);
                database_file.seekg(type_len + name_len + tbl_name_len, ios::cur);

                int root_len = (rootpage_serial <= 4) ? rootpage_serial : 0;
                database_file.seekg(root_len, ios::cur);

                int sql_len = (sql_serial >= 13 and sql_serial % 2) ? (sql_serial - 13) / 2 : 0;
                string sql_text(sql_len, ' ');
                database_file.read(&sql_text[0], sql_len);

                vector<string> table_columns = parse_columns_from_sql(sql_text);

                vector<string> target_columns;
                bool is_count_query = false;

                if (cmd_lower.find("select ") == 0)
                {
                    size_t select_pos = 7;
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
                        target_col_indices.push_back(idx);
                    }
                }

                int where_col_idx = -1;
                if (has_where)
                {
                    string lower_where_col = where_col;
                    transform(lower_where_col.begin(), lower_where_col.end(), lower_where_col.begin(), ::tolower);
                    for (size_t c = 0; c < table_columns.size(); c++)
                    {
                        string col_lower = table_columns[c];
                        transform(col_lower.begin(), col_lower.end(), col_lower.begin(), ::tolower);
                        if (col_lower == lower_where_col)
                        {
                            where_col_idx = c;
                            break;
                        }
                    }
                }

                int64_t total_rows_counted = 0;

                // --- NEW LOGIC: Ask our recursion map for all the leaf pages ---
                vector<int> all_leaf_pages = get_leaf_pages(database_file, target_root_page, page_size);

                // Now loop through every single leaf page we found
                for (int current_leaf_page : all_leaf_pages)
                {

                    streamoff data_page_offset = (current_leaf_page - 1) * (streamoff)page_size;

                    database_file.seekg(data_page_offset + 3);
                    char leaf_cnt_buf[2];
                    database_file.read(leaf_cnt_buf, 2);
                    unsigned short data_cell_cnt = (static_cast<unsigned char>(leaf_cnt_buf[1]) | (static_cast<unsigned char>(leaf_cnt_buf[0]) << 8));

                    database_file.seekg(data_page_offset + 8);
                    vector<unsigned short> data_cellpos(data_cell_cnt);
                    for (int j = 0; j < data_cell_cnt; j++)
                    {
                        char ptr[2];
                        database_file.read(ptr, 2);
                        data_cellpos[j] = (static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8));
                    }

                    // Scan the cells inside this specific leaf page
                    for (int j = 0; j < data_cell_cnt; j++)
                    {
                        database_file.seekg(data_page_offset + data_cellpos[j]);

                        int64_t payload_size = 0, row_id = 0;
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
                                    database_file.seekg(data_len, ios::cur);
                                    row_values[col] = "[IntData]";
                                }
                            }
                        }

                        if (has_where && where_col_idx != -1)
                        {
                            if (row_values[where_col_idx] != where_val)
                            {
                                continue;
                            }
                        }

                        if (is_count_query)
                        {
                            total_rows_counted++;
                            continue;
                        }

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
                        }
                        cout << output_line << endl;
                    }
                } // End of the leaf page loop

                if (is_count_query)
                {
                    cout << total_rows_counted << endl;
                }
                break; // Break from the schema search loop
            }
        }
    }
    return 0;
}