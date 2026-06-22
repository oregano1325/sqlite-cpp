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

string trim(const string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// --- NEW MAGIC 1: FAST INDEX SEARCH ---
// Parses an Index B-Tree to quickly find the exact row_ids that match our WHERE clause
void get_rowids_from_index(ifstream &db, int page_num, int page_size, const string &target_val, vector<int64_t> &found_rowids)
{
    streamoff offset = (page_num - 1) * (streamoff)page_size;
    db.seekg(offset);
    char type_flag;
    db.read(&type_flag, 1);

    db.seekg(offset + 3);
    char cnt_buf[2];
    db.read(cnt_buf, 2);
    int cell_cnt = (static_cast<unsigned char>(cnt_buf[1]) | (static_cast<unsigned char>(cnt_buf[0]) << 8));

    int right_page = 0;
    int cell_ptr_offset = 8;
    if (type_flag == 0x02)
    { // Interior Index Page
        db.seekg(offset + 8);
        char r_buf[4];
        db.read(r_buf, 4);
        right_page = (static_cast<unsigned char>(r_buf[3]) | (static_cast<unsigned char>(r_buf[2]) << 8) |
                      (static_cast<unsigned char>(r_buf[1]) << 16) | (static_cast<unsigned char>(r_buf[0]) << 24));
        cell_ptr_offset = 12;
    }

    db.seekg(offset + cell_ptr_offset);
    vector<unsigned short> cellpos(cell_cnt);
    for (int i = 0; i < cell_cnt; i++)
    {
        char ptr[2];
        db.read(ptr, 2);
        cellpos[i] = (static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8));
    }

    for (int i = 0; i < cell_cnt; i++)
    {
        db.seekg(offset + cellpos[i]);

        int left_child = 0;
        if (type_flag == 0x02)
        {
            char l_buf[4];
            db.read(l_buf, 4);
            left_child = (static_cast<unsigned char>(l_buf[3]) | (static_cast<unsigned char>(l_buf[2]) << 8) |
                          (static_cast<unsigned char>(l_buf[1]) << 16) | (static_cast<unsigned char>(l_buf[0]) << 24));
        }

        int64_t payload_size = 0;
        read_varint(db, payload_size);

        streampos payload_start = db.tellg();
        int64_t header_sz = 0;
        read_varint(db, header_sz);

        vector<int64_t> stypes;
        streampos curr = db.tellg();
        while (curr < payload_start + (streamoff)header_sz)
        {
            int64_t st;
            read_varint(db, st);
            stypes.push_back(st);
            curr = db.tellg();
        }

        // Read the indexed key string
        db.seekg(payload_start + (streamoff)header_sz);
        string key_val = "";
        int64_t stype = stypes[0];
        int data_len = (stype >= 13 && stype % 2 != 0) ? (stype - 13) / 2 : 0;
        if (data_len > 0)
        {
            char *buf = new char[data_len + 1];
            db.read(buf, data_len);
            buf[data_len] = '\0';
            key_val = string(buf);
            delete[] buf;
        }

        // Read the rowid attached to this key
        int64_t cell_rowid = 0;
        if (stypes.size() > 1)
        {
            int64_t id_type = stypes.back();
            // skip to the last element
            db.seekg(payload_start + (streamoff)header_sz);
            for (size_t k = 0; k < stypes.size() - 1; k++)
            {
                int64_t st = stypes[k];
                int len = 0;
                if (st >= 13 && st % 2 != 0)
                    len = (st - 13) / 2;
                else if (st == 1)
                    len = 1;
                else if (st == 2)
                    len = 2;
                else if (st == 3)
                    len = 3;
                else if (st == 4)
                    len = 4;
                else if (st == 5)
                    len = 6;
                else if (st == 6)
                    len = 8;
                db.seekg(len, ios::cur);
            }

            int len = 0;
            if (id_type == 1)
                len = 1;
            else if (id_type == 2)
                len = 2;
            else if (id_type == 3)
                len = 3;
            else if (id_type == 4)
                len = 4;
            else if (id_type == 5)
                len = 6;
            else if (id_type == 6)
                len = 8;

            if (len > 0)
            {
                for (int b_idx = 0; b_idx < len; b_idx++)
                {
                    char b;
                    db.read(&b, 1);
                    cell_rowid = (cell_rowid << 8) | (unsigned char)b;
                }
            }
            else if (id_type == 8)
                cell_rowid = 0;
            else if (id_type == 9)
                cell_rowid = 1;
        }

        if (key_val == target_val)
        {
            found_rowids.push_back(cell_rowid);
        }

        // Optimization: because the index is sorted alphabetically, we can cleanly prune branches!
        if (type_flag == 0x02)
        {
            if (target_val <= key_val)
            {
                get_rowids_from_index(db, left_child, page_size, target_val, found_rowids);
            }
            if (target_val < key_val)
            {
                return; // Early exit. We know it will never appear further right.
            }
        }
    }

    if (type_flag == 0x02 && right_page != 0)
    {
        get_rowids_from_index(db, right_page, page_size, target_val, found_rowids);
    }
}

// --- NEW MAGIC 2: O(LOG N) TABLE SCAN ---
// Instead of scanning the whole table, we jump exactly to the page holding our target_rowid
void print_row_by_id(ifstream &db, int page_num, int page_size, int64_t target_rowid, const vector<int> &target_col_indices, const vector<string> &table_columns)
{
    streamoff offset = (page_num - 1) * (streamoff)page_size;
    db.seekg(offset);
    char type_flag;
    db.read(&type_flag, 1);

    db.seekg(offset + 3);
    char cnt_buf[2];
    db.read(cnt_buf, 2);
    int cell_cnt = (static_cast<unsigned char>(cnt_buf[1]) | (static_cast<unsigned char>(cnt_buf[0]) << 8));

    if (type_flag == 0x05)
    { // Interior Table Page
        db.seekg(offset + 8);
        char right_buf[4];
        db.read(right_buf, 4);
        int right_page = (static_cast<unsigned char>(right_buf[3]) | (static_cast<unsigned char>(right_buf[2]) << 8) |
                          (static_cast<unsigned char>(right_buf[1]) << 16) | (static_cast<unsigned char>(right_buf[0]) << 24));

        db.seekg(offset + 12);
        vector<unsigned short> cellpos(cell_cnt);
        for (int i = 0; i < cell_cnt; i++)
        {
            char p[2];
            db.read(p, 2);
            cellpos[i] = (static_cast<unsigned char>(p[1]) | (static_cast<unsigned char>(p[0]) << 8));
        }

        for (int i = 0; i < cell_cnt; i++)
        {
            db.seekg(offset + cellpos[i]);
            char ptr_buf[4];
            db.read(ptr_buf, 4);
            int left_child = (static_cast<unsigned char>(ptr_buf[3]) | (static_cast<unsigned char>(ptr_buf[2]) << 8) |
                              (static_cast<unsigned char>(ptr_buf[1]) << 16) | (static_cast<unsigned char>(ptr_buf[0]) << 24));

            int64_t cell_rowid;
            read_varint(db, cell_rowid);

            // Drop down the tree mathematically
            if (target_rowid <= cell_rowid)
            {
                print_row_by_id(db, left_child, page_size, target_rowid, target_col_indices, table_columns);
                return;
            }
        }
        print_row_by_id(db, right_page, page_size, target_rowid, target_col_indices, table_columns);
    }
    else if (type_flag == 0x0D)
    { // Leaf Table Page
        db.seekg(offset + 8);
        vector<unsigned short> cellpos(cell_cnt);
        for (int i = 0; i < cell_cnt; i++)
        {
            char p[2];
            db.read(p, 2);
            cellpos[i] = (static_cast<unsigned char>(p[1]) | (static_cast<unsigned char>(p[0]) << 8));
        }

        for (int i = 0; i < cell_cnt; i++)
        {
            db.seekg(offset + cellpos[i]);
            int64_t payload_size = 0, cell_rowid = 0;
            read_varint(db, payload_size);
            read_varint(db, cell_rowid);

            if (cell_rowid == target_rowid)
            {
                // WE FOUND THE EXACT ROW!
                streampos payload_start = db.tellg();
                int64_t header_sz = 0;
                read_varint(db, header_sz);

                vector<int64_t> serial_types;
                streampos curr = db.tellg();
                while (curr < payload_start + (streamoff)header_sz)
                {
                    int64_t st;
                    read_varint(db, st);
                    serial_types.push_back(st);
                    curr = db.tellg();
                }

                db.seekg(payload_start + (streamoff)header_sz);
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
                            db.read(col_val, data_len);
                            col_val[data_len] = '\0';
                            row_values[col] = string(col_val);
                            delete[] col_val;
                        }
                        else
                        {
                            db.seekg(data_len, ios::cur);
                            row_values[col] = "[IntData]";
                        }
                    }

                    if (col < table_columns.size())
                    {
                        string current_col_name = table_columns[col];
                        transform(current_col_name.begin(), current_col_name.end(), current_col_name.begin(), ::tolower);
                        if (current_col_name == "id" && stype == 0)
                        {
                            row_values[col] = to_string(cell_rowid);
                        }
                    }
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
                        output_line += "|";
                }
                cout << output_line << endl;
                return;
            }
        }
    }
}

// Our old full table scan for when no index exists
vector<int> get_leaf_pages(ifstream &db, int page_num, int page_size)
{
    vector<int> leaves;
    streamoff offset = (page_num - 1) * (streamoff)page_size;
    db.seekg(offset);
    char type_flag;
    db.read(&type_flag, 1);
    if (type_flag == 0x0D)
    {
        leaves.push_back(page_num);
    }
    else if (type_flag == 0x05)
    {
        db.seekg(offset + 3);
        char cnt_buf[2];
        db.read(cnt_buf, 2);
        unsigned short cell_cnt = (static_cast<unsigned char>(cnt_buf[1]) | (static_cast<unsigned char>(cnt_buf[0]) << 8));

        db.seekg(offset + 8);
        char right_buf[4];
        db.read(right_buf, 4);
        int right_page = (static_cast<unsigned char>(right_buf[3]) | (static_cast<unsigned char>(right_buf[2]) << 8) |
                          (static_cast<unsigned char>(right_buf[1]) << 16) | (static_cast<unsigned char>(right_buf[0]) << 24));

        db.seekg(offset + 12);
        vector<unsigned short> cellpos(cell_cnt);
        for (int i = 0; i < cell_cnt; i++)
        {
            char ptr[2];
            db.read(ptr, 2);
            cellpos[i] = (static_cast<unsigned char>(ptr[1]) | (static_cast<unsigned char>(ptr[0]) << 8));
        }
        for (int i = 0; i < cell_cnt; i++)
        {
            db.seekg(offset + cellpos[i]);
            char left_buf[4];
            db.read(left_buf, 4);
            int left_page = (static_cast<unsigned char>(left_buf[3]) | (static_cast<unsigned char>(left_buf[2]) << 8) |
                             (static_cast<unsigned char>(left_buf[1]) << 16) | (static_cast<unsigned char>(left_buf[0]) << 24));
            vector<int> child_leaves = get_leaf_pages(db, left_page, page_size);
            leaves.insert(leaves.end(), child_leaves.begin(), child_leaves.end());
        }
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
        string target_table = "", where_col = "", where_val = "";
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

        int64_t target_root_page = 0;
        int64_t target_index_root_page = 0;
        vector<string> table_columns;
        vector<string> target_columns;
        bool is_count_query = false;

        // Pass 1: Parse sqlite_schema to find both the table AND its index
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
            char *type_str = new char[type_len + 1];
            database_file.read(type_str, type_len);
            type_str[type_len] = '\0';
            char *name_str = new char[name_len + 1];
            database_file.read(name_str, name_len);
            name_str[name_len] = '\0';
            char *tbl_name_str = new char[tbl_name_len + 1];
            database_file.read(tbl_name_str, tbl_name_len);
            tbl_name_str[tbl_name_len] = '\0';

            string c_type(type_str);
            string c_name(name_str);
            string c_tbl_name(tbl_name_str);
            delete[] type_str;
            delete[] name_str;
            delete[] tbl_name_str;

            // Extract the Root Page Variable for both Table AND Index
            int64_t rp = 0;
            if (rootpage_serial == 1)
                rp = (signed char)database_file.get();
            else if (rootpage_serial == 2)
            {
                char buf[2];
                database_file.read(buf, 2);
                rp = (static_cast<unsigned char>(buf[1]) | (static_cast<unsigned char>(buf[0]) << 8));
            }
            else if (rootpage_serial == 4)
            {
                char buf[4];
                database_file.read(buf, 4);
                rp = (static_cast<unsigned char>(buf[3]) | (static_cast<unsigned char>(buf[2]) << 8) | (static_cast<unsigned char>(buf[1]) << 16) | (static_cast<unsigned char>(buf[0]) << 24));
            }

            if (c_type == "table" && c_name == target_table)
            {
                target_root_page = rp;

                int root_len = (rootpage_serial <= 4) ? rootpage_serial : 0;
                database_file.seekg(root_len, ios::cur);
                int sql_len = (sql_serial >= 13 and sql_serial % 2) ? (sql_serial - 13) / 2 : 0;
                string sql_text(sql_len, ' ');
                database_file.read(&sql_text[0], sql_len);
                table_columns = parse_columns_from_sql(sql_text);
            }
            else if (c_type == "index" && c_tbl_name == target_table)
            {
                // If the index's name contains our WHERE clause column, we snag its root page
                string lower_idx_name = c_name;
                transform(lower_idx_name.begin(), lower_idx_name.end(), lower_idx_name.begin(), ::tolower);
                if (has_where && lower_idx_name.find(where_col) != string::npos)
                {
                    target_index_root_page = rp;
                }
            }
        }

        // Setup the specific columns we want to SELECT
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

        // --- BRANCHING LOGIC: Do we use the Index or the Full Table Scan? ---
        if (has_where && target_index_root_page != 0)
        {
            // STEP 1: Search the specific Index Tree
            vector<int64_t> matching_rowids;
            get_rowids_from_index(database_file, target_index_root_page, page_size, where_val, matching_rowids);

            if (is_count_query)
            {
                cout << matching_rowids.size() << endl;
            }
            else
            {
                // STEP 2: Jump instantly to the matching records in the Table Tree
                for (int64_t rowid : matching_rowids)
                {
                    print_row_by_id(database_file, target_root_page, page_size, rowid, target_col_indices, table_columns);
                }
            }
        }
    }
    return 0;
}