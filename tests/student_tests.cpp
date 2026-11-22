// Your M4 tests (specification, Section 6).
#include "aiws/gui/main_window.hpp"
#include "aiws/gui/build_worker.hpp"
#include "aiws/document.hpp"
#include <QElapsedTimer>
#include <QProcess>
#include <QApplication>
#include <utility>
#include <QtTest/QtTest>
#include <QFile>
#include <QLabel>
#include <QTemporaryDir>
#include <QMetaObject>

using aiws::gui::MainWindow;
using aiws::Workspace;
using aiws::Document;
using aiws::gui::BuildWorker;
using aiws::gui::BuildRequest;
using aiws::gui::BuildResult;
class StudentTests: public QObject{
    Q_OBJECT
    private:
    static bool create_test_document(const QString& path){
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly)){
            return false;
        }
        return file.write("This is a test document that contains" "multiple searchable words and paragraphs. \n") > 0;
    }
    static bool has_label_text(const MainWindow& window, const QString& expected){
        for(QLabel* label:window.findChildren<QLabel*>()){
            if(label->text().contains(expected)){
                return true;
            }
        }
        return false;
    }
    private slots:
    void state_corpus_after_doc_add(){ //Testing R2: rejecting build after change in documents
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString first = directory.filePath("first.txt");
        QString second = directory.filePath("second.txt");
        QVERIFY(create_test_document(first));
        QVERIFY(create_test_document(second));
        MainWindow window;
        window.set_build_delay_ms(1000); //Delay allows changes
        QVERIFY(window.add_document_file(first));
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QVERIFY(has_label_text(window, "Building corpus in background"));
        QVERIFY(window.add_document_file(second)); //adding document invalidates requested build
        QVERIFY(has_label_text(window, "Corpus: Not built"));
        QVERIFY(!has_label_text(window, "Corpus built successfully."));
        window.set_build_delay_ms(0); //verifying that app permits another build
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(has_label_text(window, "Corpus built successfully."), 3000);
       

    }
    void stale_corpus_after_reset(){
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString first = directory.filePath("first.txt");
        QVERIFY(create_test_document(first));
        MainWindow window;
        window.set_build_delay_ms(500);
        QVERIFY(window.add_document_file(first));
      
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&window, "reset_application", Qt::DirectConnection));
        QVERIFY(has_label_text(window, "Application reset."));
        QVERIFY (!has_label_text(window, "Corpus built successfully."));
        //Verifying that app can build again post-reset
        QVERIFY(window.add_document_file(first));
        window.set_build_delay_ms(0);
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(has_label_text(window, "Corpus built successfully."), 3000);
    
    
    }
    void build_failure_is_reported(){
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString first = directory.filePath("first.txt");
        QVERIFY(create_test_document(first));
        MainWindow m_window;
        m_window.set_build_delay_ms(100);
        QVERIFY(m_window.add_document_file(first));
        m_window.set_fail_nxt_build_for_testing(true);
        QVERIFY(QMetaObject::invokeMethod(&m_window, "build_corpus", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(has_label_text(m_window, "Corpus: Failed"), 3000);
        QVERIFY(QMetaObject::invokeMethod(&m_window, "build_corpus", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(has_label_text(m_window, "Corpus built successfully."), 3000);
    }
    void cancel_during_build(){
        Workspace original;
        for(int i = 0; i < 10; ++i){
            std::string name = "document_" + std::to_string(i);
            original.add_document(Document(name, name,"A searchable test document."));
        }
        Workspace replace_ws;
        replace_ws.add_document(Document("replacement", "Replacement", "This is the replacement corpus."));
        QObject receiver;
        bool replacement_finished = false;
        bool original_finished = false;
        BuildWorker worker(&receiver, [&](BuildResult result){
            if(result.revision == 1){
                original_finished = true;
            }
            if(result.revision == 2 && result.core && result.error.empty()){
                replacement_finished = true;
            }
        });
        //Ten docs at 500ms = 5 seconds
        BuildRequest first{original, 1, false, 500};
        worker.submit(std::move(first));
        //Establishing that processing begins
        QVERIFY(worker.wait_until_running_4_test(2000));
        QElapsedTimer timer;
        timer.start();
        worker.cancel(); //cancelling original build
        BuildRequest second{replace_ws, 2, false, 0};
        worker.submit(std::move(second));

        //replacement should not wait 4 old build
        QTRY_VERIFY_WITH_TIMEOUT(replacement_finished, 2000);
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(!original_finished);
        worker.stop();
    }
    void shutdown_busy_worker(){
        Workspace workspace;
        //Long running corpus build
        for(int i = 0; i < 10; ++i){
            std::string name = "document_" + std::to_string(i);
            workspace.add_document(Document(name, name, "A searchable test document."));

        }
        QObject receiver;
        BuildWorker worker(&receiver, [](BuildResult){});
        BuildRequest request{workspace, 1, false, 500};
        worker.submit(std::move(request));
        //confirming that worker has started processing
        QVERIFY(worker.wait_until_running_4_test(2000));
        QElapsedTimer timer;
        timer.start();
        //Requesting shutdown during the build
        worker.stop();
        //Shutdown does not wait for all 10 docs
        QVERIFY(timer.elapsed() < 1500);
    }
};

//QTEST_MAIN(StudentTests)
int main(int argc, char* argv[]){
    QApplication app(argc, argv);
    //process used to test idle worker shutdown
    if(app.arguments().contains("--test-idle-shutdown")){
        QObject receiver;
        BuildWorker worker(&receiver, [](BuildResult) {});
        //establishing that the worker is waiting
        if(!worker.wait_until_idle_4_test(2000)){
            return 1;
        }
        worker.stop(); //should return promptly
        return 0;
    }
    QProcess process; //process launches the shutdown test
    process.start(QCoreApplication::applicationFilePath(), QStringList{"--test-idle-shutdown"});
    if(!process.waitForStarted(2000)){
        qCritical("Unable to start shutdown testing.");
        return 1;
    }
    //hanging shutdown detection
    if(!process.waitForFinished(3000)){
        process.kill();
        process.waitForFinished(2000);
        qCritical("FAIL: Idle worker shutdown timed out.");
        return 1;
    }
    if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0){
        qCritical("FAIL: Idle worker shutdown failed.");
        return 1;

    }
    qInfo("Pass: Idle worker shutdown.");
    //Running remaining Qt tests
    StudentTests tests;
    return QTest::qExec(&tests, argc, argv);

}
#include "student_tests.moc"
