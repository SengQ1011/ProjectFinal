#ifndef PYTHONAIMANAGER_H
#define PYTHONAIMANAGER_H

#include <QObject>
#include <QProcess>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>

class PythonAiManager : public QObject {
    Q_OBJECT
public:
    explicit PythonAiManager(QObject *parent = nullptr);
    ~PythonAiManager();

public slots:
    void start();  // 啟動 Python 進程
    void stop();   // 停止 Python 進程

signals:
    void frameReady(QImage img);
    void detectionAlert(QString type, double confidence);
    void errorOccurred(QString msg);

private slots:
    void handleReadyRead();
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess *m_process;
    QByteArray m_buffer;
    bool m_isRunning;
    void parseLine(const QByteArray &line);
};

#endif // PYTHONAIMANAGER_H
