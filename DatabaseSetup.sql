CREATE DATABASE ByteKeeperDB;
GO

USE ByteKeeperDB;
GO

CREATE TABLE Categories (
    CategoryID INT IDENTITY(1,1) PRIMARY KEY,
    CategoryName NVARCHAR(100) NOT NULL
);

CREATE TABLE Users (
    UserID INT IDENTITY(1,1) PRIMARY KEY,
    UserName NVARCHAR(100) NOT NULL
);

CREATE TABLE Resources (
    ResourceID INT IDENTITY(1,1) PRIMARY KEY,
    Name NVARCHAR(255) NOT NULL,
    Size BIGINT NOT NULL,
    CategoryID INT NOT NULL,
    OwnerID INT NOT NULL,
    isDeleted BIT DEFAULT 0,
    UploadDate DATETIME DEFAULT GETDATE(),
    FOREIGN KEY (CategoryID) REFERENCES Categories(CategoryID),
    FOREIGN KEY (OwnerID) REFERENCES Users(UserID)
);

CREATE TABLE Logs (
    LogID INT IDENTITY(1,1) PRIMARY KEY,
    ActionDate DATETIME DEFAULT GETDATE(),
    ActionDescription NVARCHAR(255) NOT NULL
);

-- Тестовые данные для старта
INSERT INTO Categories (CategoryName) VALUES ('Документы'), ('Медиа'), ('Архивы');
INSERT INTO Users (UserName) VALUES ('Максим'), ('Admin');
GO