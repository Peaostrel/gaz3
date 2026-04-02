#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <iomanip>
#include <regex>
#include <fstream>

// Указываем линковщику подключить библиотеку ODBC
#pragma comment(lib, "odbc32.lib")

using namespace std;

// === Глобальные переменные ===
SQLHENV hEnv = SQL_NULL_HENV;
SQLHDBC hDbc = SQL_NULL_HDBC;
SQLHSTMT hStmt = SQL_NULL_HSTMT;
string currentDSN = "ByteKeeperDSN";

// === Утилиты ===
void SetColor(int textColor) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, textColor);
}

wstring StringToWString(const string& s) {
    int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, buf, len);
    wstring r(buf);
    delete[] buf;
    return r;
}

wstring Truncate(const wstring& str) {
    if (str.length() > 15) return str.substr(0, 15) + L"...";
    return str;
}

// === Работа с БД ===
void DisconnectDB() {
    if (hStmt) { SQLFreeHandle(SQL_HANDLE_STMT, hStmt); hStmt = SQL_NULL_HSTMT; }
    if (hDbc) { SQLDisconnect(hDbc); SQLFreeHandle(SQL_HANDLE_DBC, hDbc); hDbc = SQL_NULL_HDBC; }
    if (hEnv) { SQLFreeHandle(SQL_HANDLE_ENV, hEnv); hEnv = SQL_NULL_HENV; }
}

bool ConnectDB(const string& dsnName) {
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    wstring wDsn = StringToWString(dsnName);
    if (SQL_SUCCEEDED(SQLConnect(hDbc, (SQLWCHAR*)wDsn.c_str(), SQL_NTS, NULL, 0, NULL, 0))) {
        SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
        currentDSN = dsnName;
        return true;
    }
    DisconnectDB();
    return false;
}

void LogAction(const wstring& actionDesc) {
    SQLHSTMT hLog;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hLog);
    wstring query = L"INSERT INTO Logs (ActionDescription) VALUES (?)";
    SQLPrepare(hLog, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb = SQL_NTS;
    SQLBindParameter(hLog, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)actionDesc.c_str(), 0, &cb);
    SQLExecute(hLog);
    SQLFreeHandle(SQL_HANDLE_STMT, hLog);
}

// === Группа Г: Смена базы на лету ===
void ChangeDatabase() {
    string newDsn;
    cout << "Введите имя нового DSN для подключения: ";
    cin >> newDsn;
    DisconnectDB();
    if (ConnectDB(newDsn)) {
        SetColor(2); cout << "Успешно подключено к " << newDsn << "\n"; SetColor(7);
    } else {
        SetColor(4); cout << "Ошибка подключения! Возврат к старому DSN.\n"; SetColor(7);
        ConnectDB(currentDSN);
    }
}

// === Группа Б: Очистка старых данных (DATEDIFF) ===
void CleanOldData() {
    SQLHSTMT hClean;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hClean);
    // Предлагаем удалить записи старше 30 дней
    wstring query = L"UPDATE Resources SET isDeleted = 1 WHERE DATEDIFF(day, UploadDate, GETDATE()) > 30 AND isDeleted = 0";
    // ИСПРАВЛЕНО: SQLExecDirect вместо SQLExecuteDirect
    if (SQL_SUCCEEDED(SQLExecDirect(hClean, (SQLWCHAR*)query.c_str(), SQL_NTS))) {
        SQLLEN rowCount = 0;
        SQLRowCount(hClean, &rowCount);
        SetColor(14); cout << "Очистка завершена. Отправлено в корзину старых файлов: " << rowCount << "\n"; SetColor(7);
        if (rowCount > 0) LogAction(L"Очистка старых файлов (>1 месяца)");
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hClean);
}

// === Группа А и Д: Интеллектуальный поиск (LIKE) ===
void SearchResources(const wstring& searchInput) {
    SQLHSTMT hSearch;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hSearch);
    
    // Заменяем пробелы на '%' для мульти-поиска (например: "отчет 2023" -> "%отчет%2023%")
    wstring mask = L"%" + regex_replace(searchInput, wregex(L" "), L"%") + L"%";

    wstring query = L"SELECT ResourceID, Name, Size FROM Resources WHERE Name LIKE ? AND isDeleted = 0";
    SQLPrepare(hSearch, (SQLWCHAR*)query.c_str(), SQL_NTS);
    
    SQLLEN cbMask = SQL_NTS;
    SQLBindParameter(hSearch, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)mask.c_str(), 0, &cbMask);
    
    if (SQL_SUCCEEDED(SQLExecute(hSearch))) {
        SQLINTEGER id; SQLWCHAR name[256]; SQLBIGINT size; 
        SQLLEN cbId = 0, cbName = 0, cbSize = 0;
        SQLBindCol(hSearch, 1, SQL_C_SLONG, &id, 0, &cbId);
        SQLBindCol(hSearch, 2, SQL_C_WCHAR, name, sizeof(name), &cbName);
        SQLBindCol(hSearch, 3, SQL_C_SBIGINT, &size, 0, &cbSize);

        cout << "\n--- Результаты поиска ---\n";
        bool found = false;
        while (SQLFetch(hSearch) == SQL_SUCCESS) {
            found = true;
            wcout << L"ID: " << id << L" | Имя: " << Truncate(wstring(name)) << L" | Размер: " << size << L" B\n";
        }
        if (!found) cout << "Совпадений не найдено.\n";
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hSearch);
}

// === Группа В и Д: Экспорт (CSV и TXT) ===
void ExportData() {
    SQLHSTMT hExp;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hExp);
    wstring query = L"SELECT ResourceID, Name, Size FROM Resources WHERE isDeleted = 0";
    // ИСПРАВЛЕНО: SQLExecDirect вместо SQLExecuteDirect
    SQLExecDirect(hExp, (SQLWCHAR*)query.c_str(), SQL_NTS);

    ofstream csv("export.csv");
    ofstream txt("report.txt");
    
    csv << "ID;Имя файла;Размер(Байт)\n";
    txt << left << setw(10) << "ID" << setw(30) << "Имя файла" << "Размер\n";
    txt << string(60, '-') << "\n";

    SQLINTEGER id; SQLWCHAR name[256]; SQLBIGINT size; SQLLEN cbId=0, cbName=0, cbSize=0;
    SQLBindCol(hExp, 1, SQL_C_SLONG, &id, 0, &cbId);
    SQLBindCol(hExp, 2, SQL_C_WCHAR, name, sizeof(name), &cbName);
    SQLBindCol(hExp, 3, SQL_C_SBIGINT, &size, 0, &cbSize);

    int count = 0;
    while (SQLFetch(hExp) == SQL_SUCCESS) {
        // Чтобы wstring нормально писался в char потоки, конвертируем обратно
        int len = WideCharToMultiByte(CP_ACP, 0, name, -1, NULL, 0, NULL, NULL);
        char* buf = new char[len];
        WideCharToMultiByte(CP_ACP, 0, name, -1, buf, len, NULL, NULL);
        string cName(buf); delete[] buf;

        csv << id << ";" << cName << ";" << size << "\n";
        txt << left << setw(10) << id << setw(30) << cName << size << "\n";
        count++;
    }
    
    csv.close(); txt.close();
    SetColor(2); cout << "Экспорт завершен! Выгружено " << count << " записей в export.csv и report.txt\n"; SetColor(7);
    SQLFreeHandle(SQL_HANDLE_STMT, hExp);
}

int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);

    if (!ConnectDB(currentDSN)) {
        SetColor(4); cout << "Критическая ошибка: Нет подключения к БД.\n"; SetColor(7); return 1;
    }

    int choice = 0;
    while (choice != 9) {
        cout << "\n1. Поиск (LIKE)\n";
        cout << "2. Очистить старые файлы (DATEDIFF)\n";
        cout << "3. Экспорт в CSV и TXT\n";
        cout << "4. Сменить базу данных (DSN)\n";
        cout << "9. Выход\nВыбор: ";
        
        if (!(cin >> choice)) {
            cin.clear(); cin.ignore(10000, '\n');
            SetColor(4); cout << "Ошибка: Введите число!\n"; SetColor(7);
            continue;
        }

        if (choice == 1) {
            string req;
            cout << "Введите фразу для поиска: ";
            cin.ignore();
            getline(cin, req);
            SearchResources(StringToWString(req));
        }
        else if (choice == 2) CleanOldData();
        else if (choice == 3) ExportData();
        else if (choice == 4) ChangeDatabase();
        else if (choice == 9) break;
    }

    DisconnectDB();
    return 0;
}