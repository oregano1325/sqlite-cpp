# Build Your Own SQLite (C++)

![progress-banner](https://backend.codecrafters.io/progress/sqlite/98aac2be-ee51-4e99-8fbb-319bda5ba4d4)

A custom SQLite database engine built from scratch in **C++** as part of the **CodeCrafters "Build Your Own SQLite" Challenge**.

This project focuses on understanding how relational databases work internally by implementing core SQLite features without using any external database libraries.

## Project Overview

Built a lightweight SQLite clone capable of reading real `.db` files and executing basic SQL queries such as `SELECT`.

The implementation covers:

- Parsing SQLite database file headers and page structures
- Decoding SQLite binary records using Varints and Record Headers
- Reading and interpreting SQLite schema information
- Implementing recursive B-Tree traversal for table and index structures
- Executing SQL queries directly on raw database files
- Supporting table scans and index-based lookups

## Technologies Used

- **Language:** C++
- **Concepts:** Database Internals, Binary Parsing, B-Trees, File Formats, SQL Execution
- **Platform:** CodeCrafters

## How It Works

The program directly reads SQLite database files, processes their binary format, extracts table metadata, and retrieves records by traversing the underlying B-Tree storage structure.

It recreates the fundamental operations performed by a database engine:
- Storage layer parsing
- Index traversal
- Record decoding
- Query execution

## Learning Outcomes

Through this project, I gained hands-on experience with:

- How SQLite stores data internally
- Binary file parsing and serialization
- Database indexing mechanisms
- B-Tree based data retrieval
- Building a database engine from low-level components

## Running the Project

Clone the repository:

```bash
git clone <repository-url>
cd <repository-name>
