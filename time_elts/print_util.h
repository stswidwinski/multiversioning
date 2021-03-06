#ifndef PRINT_UTIL_TIME_H_
#define PRINT_UTIL_TIME_H_

#include <vector>
#include <string>
#include <iostream>
#include <cassert>

class TablePrinter {
private:
  typedef std::vector<std::string> RowData;
  std::vector<RowData> table_data;
  std::string table_header;
public:
  void set_table_header(std::string t_header) {
    table_header = t_header;
  };

  // TODO: Generate the functions for vectors using a macro.
  void add_column_headers(std::vector<std::string> headers) {
    for (auto& h : headers) {
      add_column_header(h);
    }
  };

  void add_column_header(std::string header) {
    if (table_data.size() < 1) {
      add_row();
    }

    table_data[0].push_back(header);
  };

  void add_row() {
    table_data.push_back(RowData());
  };

  void add_to_last_row(std::vector<std::string> data) {
    for (auto& d : data) {
      add_to_last_row(d);    
    }
  };

  void add_to_last_row(std::string data) {
     add_to_row(table_data.size() - 1, data);
   };
 
  void add_to_row(unsigned int row, std::vector<std::string> data) {
     for (auto& d : data) {
       add_to_row(row, d);
     }
   };
 
  void add_to_row(unsigned int row, std::string data) {
    while (table_data.size() < row) {
      add_row();
    }

    table_data[row].push_back(data);
  };

  void print_table() {
    if (table_data.size() == 0) return;

    // Find the length of the longest element within the table.
    unsigned int max_data_len = 0;
    for (auto& row : table_data) {
      for (auto& str : row) {
        max_data_len = max_data_len < str.length() ? str.length() : max_data_len;
      }
    }

    unsigned int cel_padding = 2;
    unsigned int cel_len = cel_padding * 2 + max_data_len;
    unsigned int table_len = cel_len * table_data[0].size();
    
    auto pad = [](unsigned int len, char pad_char) {
      for (unsigned int i = 0; i < len; i++) {
        std::cout << pad_char;
      }
    };

    auto write_middle = [&pad](std::string str, unsigned int cel_len) {
      unsigned int padding = (cel_len - str.length()) / 2;
      assert(padding > 1);
      std::cout << "|";
      pad(padding - 1, ' ');
      std::cout << str;
      pad(cel_len - padding - str.length(), ' ');
    };

    auto hor_rule = [&pad, &table_len]() {
      std::cout << "|";
      pad(table_len - 1, '-');
      std::cout << std::endl;
    };

    // write the table title.
    write_middle(table_header, table_len);
    std::cout << std::endl;
    hor_rule();

    // write the data.
    for (auto& row : table_data) {
      for (auto& str: row) {
        write_middle(str, cel_len);
      }
      std::cout <<  std::endl;
      hor_rule();
    }
  };
};

#endif // PRINT_UTIL_TIME_H_
