#define UNICODE
#define _UNICODE

#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <iomanip>
#include <regex>
#include <fstream>

#pragma comment(lib, "odbc32.lib")

using namespace std;

SQLHENV hEnv = SQL_NULL_HENV;
SQLHDBC hDbc = SQL_NULL_HDBC;
SQLHSTMT hStmt = SQL_NULL_HSTMT;

string currentConnStr = "DRIVER={SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=ByteKeeperDB;Trusted_Connection=yes;";

void SetColor(int textColor) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), textColor);
}

wstring StringToWString(const string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf, len);
    wstring r(buf); delete[] buf; return r;
}

string WStringToString(const wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    char* buf = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, buf, len, NULL, NULL);
    string r(buf); delete[] buf; return r;
}

wstring Truncate(const wstring& str) {
    if (str.length() > 15) return str.substr(0, 15) + L"...";
    return str;
}

void DisconnectDB() {
    if (hStmt) { SQLFreeHandle(SQL_HANDLE_STMT, hStmt); hStmt = SQL_NULL_HSTMT; }
    if (hDbc) { SQLDisconnect(hDbc); SQLFreeHandle(SQL_HANDLE_DBC, hDbc); hDbc = SQL_NULL_HDBC; }
    if (hEnv) { SQLFreeHandle(SQL_HANDLE_ENV, hEnv); hEnv = SQL_NULL_HENV; }
}

bool ConnectDB(const string& connString) {
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    SQLSetConnectAttr(hDbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)3, 0);
    
    wstring wConnStr = StringToWString(connString);
    SQLWCHAR outConnStr[1024]; SQLSMALLINT outConnStrLen;
    if (SQL_SUCCEEDED(SQLDriverConnect(hDbc, NULL, (SQLWCHAR*)wConnStr.c_str(), SQL_NTS, outConnStr, 1024, &outConnStrLen, SQL_DRIVER_NOPROMPT))) {
        SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
        currentConnStr = connString;
        return true;
    }
    DisconnectDB(); return false;
}

void LogAction(const wstring& actionDesc) {
    SQLHSTMT hLog; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hLog);
    wstring query = L"INSERT INTO Logs (ActionDescription) VALUES (?)";
    SQLPrepare(hLog, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb = SQL_NTS;
    SQLBindParameter(hLog, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)actionDesc.c_str(), 0, &cb);
    SQLExecute(hLog); SQLFreeHandle(SQL_HANDLE_STMT, hLog);
}

bool IsValidName(const wstring& name) {
    return !regex_search(name, wregex(L"[\\\\/:*?\"<>|]"));
}

bool IsAllowedExtension(const wstring& name) {
    size_t pos = name.find_last_of(L".");
    if (pos == wstring::npos && name.find(L"FOLDER") != wstring::npos) return true;
    if (pos == wstring::npos) return false;
    wstring ext = name.substr(pos);
    return (ext == L".exe" || ext == L".txt" || ext == L".pdf");
}

bool IsDuplicate(const wstring& name) {
    SQLHSTMT hDup; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hDup);
    wstring query = L"SELECT COUNT(*) FROM Resources WHERE Name = ?";
    SQLPrepare(hDup, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb = SQL_NTS;
    SQLBindParameter(hDup, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)name.c_str(), 0, &cb);
    SQLExecute(hDup);
    SQLINTEGER count = 0; SQLLEN cbCount = 0;
    SQLBindCol(hDup, 1, SQL_C_SLONG, &count, 0, &cbCount);
    SQLFetch(hDup); SQLFreeHandle(SQL_HANDLE_STMT, hDup);
    return count > 0;
}

void AddResourceInteractively() {
    string nameStr; long long size; int catId = 1, ownerId = 1;
    cout << "Имя файла: "; cin >> ws; getline(cin, nameStr);
    cout << "Размер (байт): "; cin >> size;
    wstring name = StringToWString(nameStr);

    if (!IsValidName(name)) { SetColor(4); cout << "Ошибка: Имя содержит запрещенные символы.\n"; SetColor(7); return; }
    if (!IsAllowedExtension(name)) { SetColor(4); cout << "Ошибка: Разрешены только .exe, .txt, .pdf (или FOLDER).\n"; SetColor(7); return; }
    if (IsDuplicate(name)) { SetColor(4); cout << "Ошибка: Дубликат имени.\n"; SetColor(7); return; }

    SQLHSTMT hIns; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hIns);
    wstring query = L"INSERT INTO Resources (Name, Size, CategoryID, OwnerID) VALUES (?, ?, ?, ?)";
    SQLPrepare(hIns, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb1 = SQL_NTS, cb2 = 0, cb3 = 0, cb4 = 0;
    SQLBindParameter(hIns, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)name.c_str(), 0, &cb1);
    SQLBindParameter(hIns, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &size, 0, &cb2);
    SQLBindParameter(hIns, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &catId, 0, &cb3);
    SQLBindParameter(hIns, 4, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &ownerId, 0, &cb4);

    if (SQL_SUCCEEDED(SQLExecute(hIns))) {
        SetColor(2); cout << "Файл успешно добавлен.\n"; SetColor(7);
        LogAction(L"Добавлен: " + name);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hIns);
}

void ShowResourcesPaged(int page, int limit) {
    SQLHSTMT hPage; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hPage);
    wstring query = L"SELECT ResourceID, Name, Size FROM Resources WHERE isDeleted = 0 ORDER BY ResourceID OFFSET ? ROWS FETCH NEXT ? ROWS ONLY";
    SQLPrepare(hPage, (SQLWCHAR*)query.c_str(), SQL_NTS);
    int offset = (page - 1) * limit; SQLLEN cbOff = 0, cbLim = 0;
    SQLBindParameter(hPage, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &offset, 0, &cbOff);
    SQLBindParameter(hPage, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &limit, 0, &cbLim);
    SQLExecute(hPage);

    SQLINTEGER id; SQLWCHAR name[256]; SQLBIGINT size; SQLLEN cbId = 0, cbName = 0, cbSize = 0;
    SQLBindCol(hPage, 1, SQL_C_SLONG, &id, 0, &cbId);
    SQLBindCol(hPage, 2, SQL_C_WCHAR, name, sizeof(name), &cbName);
    SQLBindCol(hPage, 3, SQL_C_SBIGINT, &size, 0, &cbSize);

    cout << "\n--- Страница " << page << " ---\n";
    cout << left << setw(5) << "ID" << setw(20) << "Имя файла" << "Размер\n";
    cout << string(40, '-') << "\n";
    bool hasData = false;
    while (SQLFetch(hPage) == SQL_SUCCESS) {
        hasData = true;
        wstring originalName(name);
        wstring wName = Truncate(originalName);
        
        cout << left << setw(5) << id;
        
        // Проверяем наличие слова FOLDER в оригинальном имени, ДО обрезки
        if (originalName.find(L"FOLDER") != wstring::npos) SetColor(14);
        
        cout << setw(20) << WStringToString(wName); SetColor(7);
        cout << size << " B\n";
    }
    if (!hasData) cout << "Пусто.\n";
    SQLFreeHandle(SQL_HANDLE_STMT, hPage);
}

void ShowStatistics() {
    SQLHSTMT hStat; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStat);
    wstring query = L"SELECT COUNT(*), ISNULL(SUM(Size), 0) FROM Resources WHERE isDeleted = 0";
    SQLExecDirect(hStat, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLINTEGER count = 0; SQLBIGINT totalSize = 0; SQLLEN cb1 = 0, cb2 = 0;
    SQLBindCol(hStat, 1, SQL_C_SLONG, &count, 0, &cb1);
    SQLBindCol(hStat, 2, SQL_C_SBIGINT, &totalSize, 0, &cb2);
    if (SQLFetch(hStat) == SQL_SUCCESS) {
        SetColor(11);
        cout << "\nВсего активных файлов: " << count << " | Общий объем (байт): " << totalSize << "\n";
        SetColor(7);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hStat);
}

void SearchResources(const wstring& searchInput) {
    SQLHSTMT hSearch; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hSearch);
    wstring mask = L"%" + regex_replace(searchInput, wregex(L" "), L"%") + L"%";
    wstring query = L"SELECT ResourceID, Name, Size FROM Resources WHERE Name LIKE ? AND isDeleted = 0";
    SQLPrepare(hSearch, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cbMask = SQL_NTS;
    SQLBindParameter(hSearch, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)mask.c_str(), 0, &cbMask);
    if (SQL_SUCCEEDED(SQLExecute(hSearch))) {
        SQLINTEGER id; SQLWCHAR name[256]; SQLBIGINT size; SQLLEN cbId = 0, cbName = 0, cbSize = 0;
        SQLBindCol(hSearch, 1, SQL_C_SLONG, &id, 0, &cbId);
        SQLBindCol(hSearch, 2, SQL_C_WCHAR, name, sizeof(name), &cbName);
        SQLBindCol(hSearch, 3, SQL_C_SBIGINT, &size, 0, &cbSize);
        cout << "\n--- Результаты поиска ---\n";
        bool found = false;
        while (SQLFetch(hSearch) == SQL_SUCCESS) {
            found = true;
            cout << "ID: " << id << " | " << WStringToString(Truncate(wstring(name))) << " | " << size << " B\n";
        }
        if (!found) cout << "Не найдено.\n";
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hSearch);
}

void ToggleDeleteStatus() {
    int resourceId, flagVal;
    cout << "ID файла: "; cin >> resourceId;
    cout << "Действие (1 - в корзину, 0 - восстановить): "; cin >> flagVal;

    SQLHSTMT hToggle; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hToggle);
    wstring query = L"UPDATE Resources SET isDeleted = ? WHERE ResourceID = ?";
    SQLPrepare(hToggle, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cbFlag = 0, cbId = 0;
    SQLBindParameter(hToggle, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &flagVal, 0, &cbFlag);
    SQLBindParameter(hToggle, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &resourceId, 0, &cbId);

    if (SQL_SUCCEEDED(SQLExecute(hToggle))) {
        SQLLEN rowCount = 0; SQLRowCount(hToggle, &rowCount);
        if (rowCount > 0) {
            SetColor(14); cout << "Статус изменен.\n"; SetColor(7);
            LogAction(L"Изменен статус удаления ID: " + to_wstring(resourceId));
        } else { SetColor(4); cout << "Файл не найден.\n"; SetColor(7); }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hToggle);
}

void CleanOldData() {
    SQLHSTMT hClean; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hClean);
    wstring query = L"UPDATE Resources SET isDeleted = 1 WHERE DATEDIFF(day, UploadDate, GETDATE()) > 30 AND isDeleted = 0";
    if (SQL_SUCCEEDED(SQLExecDirect(hClean, (SQLWCHAR*)query.c_str(), SQL_NTS))) {
        SQLLEN rowCount = 0; SQLRowCount(hClean, &rowCount);
        SetColor(14); cout << "Старых файлов отправлено в корзину: " << rowCount << "\n"; SetColor(7);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hClean);
}

void ExportData() {
    SQLHSTMT hExp; SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hExp);
    SQLExecDirect(hExp, (SQLWCHAR*)L"SELECT ResourceID, Name, Size FROM Resources WHERE isDeleted = 0", SQL_NTS);
    ofstream csv("export.csv"); ofstream txt("report.txt");
    csv << "ID;Name;Size\n"; txt << left << setw(10) << "ID" << setw(30) << "Name" << "Size\n";
    SQLINTEGER id; SQLWCHAR name[256]; SQLBIGINT size; SQLLEN cbId=0, cbName=0, cbSize=0;
    SQLBindCol(hExp, 1, SQL_C_SLONG, &id, 0, &cbId);
    SQLBindCol(hExp, 2, SQL_C_WCHAR, name, sizeof(name), &cbName);
    SQLBindCol(hExp, 3, SQL_C_SBIGINT, &size, 0, &cbSize);
    int count = 0;
    while (SQLFetch(hExp) == SQL_SUCCESS) {
        string cName = WStringToString(wstring(name));
        csv << id << ";" << cName << ";" << size << "\n"; txt << left << setw(10) << id << setw(30) << cName << size << "\n"; count++;
    }
    SetColor(2); cout << "Выгружено " << count << " записей.\n"; SetColor(7);
    SQLFreeHandle(SQL_HANDLE_STMT, hExp);
}

void ChangeDatabase() {
    string newDB; cout << "Имя новой БД: "; cin >> newDB;
    string newConnStr = "DRIVER={SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=" + newDB + ";Trusted_Connection=yes;";
    DisconnectDB();
    if (ConnectDB(newConnStr)) { SetColor(2); cout << "Успех.\n"; SetColor(7); }
    else { SetColor(4); cout << "Ошибка.\n"; SetColor(7); ConnectDB(currentConnStr); }
}

int main() {
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
    
    cout << "Подключение к БД...\n";
    if (!ConnectDB(currentConnStr)) { 
        SetColor(4); cout << "Критическая ошибка: Нет БД.\n"; SetColor(7); 
        return 1; 
    }
    
    SetColor(2); cout << "Успешное подключение к SQL Server!\n"; SetColor(7);
    
    int choice = 0, page = 1;
    while (choice != 9) {
        cout << "\n========================================\n";
        cout << "1. Добавить файл\n";
        cout << "2. Список (След. страница)\n";
        cout << "3. Статистика\n";
        cout << "4. Поиск\n";
        cout << "5. Удалить/Восстановить (Корзина)\n";
        cout << "6. Экспорт (CSV/TXT)\n";
        cout << "7. Сменить БД\n";
        cout << "9. Выход\n";
        cout << "========================================\n";
        cout << "Выбор: ";
        
        if (!(cin >> choice)) { 
            cin.clear(); cin.ignore(10000, '\n'); 
            SetColor(4); cout << "Ошибка ввода. Введите число.\n"; SetColor(7); 
            continue; 
        }
        
        if (choice == 1) AddResourceInteractively();
        else if (choice == 2) ShowResourcesPaged(page++, 5);
        else if (choice == 3) ShowStatistics();
        else if (choice == 4) { string req; cout << "Фраза: "; cin.ignore(); getline(cin, req); SearchResources(StringToWString(req)); }
        else if (choice == 5) ToggleDeleteStatus();
        else if (choice == 6) ExportData();
        else if (choice == 7) ChangeDatabase();
    }
    DisconnectDB(); return 0;
}