#include "aiws/gui/main_window.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/document.hpp"
#include "aiws/gui/resettable.hpp"
#include <QLabel>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include<QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <exception>
#include <utility>
#include <QCoreApplication>
#include <QMessageBox>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <QTimer>
#include <chrono>
#include <future>
#include <thread>
#include <utility>
#include <QCloseEvent>


namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    //Serves as the central coordiation for the entire application, 
    //it owns Workspace and ProcessingCore backend objects while connecting
    //Gui components using the QT signals and slots. 
    setWindowTitle("AI Workspace");
    resize(900,900); //Setting up application window
    //Setting up widget and main Layout
    QWidget* central = new QWidget(this);
    QVBoxLayout* main_lay = new QVBoxLayout(central);
    //Setting up application title and header
    QLabel* title_label = new QLabel("AI Workspace", central);
    title_label->setAlignment(Qt::AlignCenter);
   title_label->setStyleSheet("background-color: #80002B;" "color: white;" "font-size: 20px;" "font-weight: bold;"
    "padding: 12px;");
   main_lay->addWidget(title_label);
   //Workspace component
   workspace_widget_ = new WorkspaceWidget(central);
   main_lay->addWidget(workspace_widget_, 1);
    //Search components
   search_widget_ = new SearchWidget(central);
   main_lay->addWidget(search_widget_,2);

   QHBoxLayout* lower_lay = new QHBoxLayout(); //lower portion of gui
   term_inspector_widget_ = new TermInspectorWidget(central); //constructing the term inspector
   context_widget_ = new ContextWidget(central);
   lower_lay->addWidget(term_inspector_widget_, 1);
   lower_lay->addWidget(context_widget_, 1);
   main_lay->addLayout(lower_lay, 1);
   
   //Setting up bottom status and the action buttons
   QHBoxLayout* bottom_lay = new QHBoxLayout();
   status_label_ = new QLabel("Ready.", central);
   reset_button_ = new QPushButton("Reset", central);
   exit_button_ = new QPushButton("Exit", central);
   reset_button_->setStyleSheet("background-color: #d6ac50;" "color: white;" "padding: 8px;");
   exit_button_->setStyleSheet("background-color: #800020;" "color: white;" "padding: 8px;");
   bottom_lay->addWidget(status_label_);
   bottom_lay->addStretch();
   bottom_lay->addWidget(reset_button_);
   bottom_lay->addWidget(exit_button_);
   main_lay->addLayout(bottom_lay);
  
   //Installing the central widget
   setCentralWidget(central);
   //Connecting the WorkspaceWidget elements
   connect(workspace_widget_, &WorkspaceWidget::add_document_requested, this, &MainWindow::add_document);
   connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,this, &MainWindow::build_corpus);
   connect(workspace_widget_, &WorkspaceWidget::document_selected, this, &MainWindow::document_selected);
   //Connecting the reset and exit elements
   connect(reset_button_, &QPushButton::clicked, this, &MainWindow::reset_application);
   connect(exit_button_, &QPushButton::clicked, this, &MainWindow::exit_application);
   //Conencting the search button request
   connect(search_widget_, &SearchWidget::search_requested, this, &MainWindow::search_requested);
   //Connecting query text changed
   connect(search_widget_, &SearchWidget::query_changed, this, &MainWindow::query_changed);
   //Connecting view source requested
   connect(search_widget_, &SearchWidget::source_document_requested, this, &MainWindow::source_document_requested);
   connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested, this, &MainWindow::term_inspection_requested);
   connect(search_widget_, &SearchWidget::selected_chunk_changed, term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);
   connect(context_widget_, &ContextWidget::build_context_requested, this, &MainWindow::build_context_requested);
   connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &MainWindow::shutdown_worker);
   //Initializing the workspace states
   workspace_widget_->set_corpus_status("Corpus: Not built");
   update_action_states();
 //Creating single persistent build worker, then completed builds delivered back to GUI thread
 build_worker_ = std::make_unique<BuildWorker>(this, [this](BuildResult result){
    handle_build_result(std::move(result));});
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}


void MainWindow::add_document() {
    QString file_path = QFileDialog::getOpenFileName(this, "Add Document", QString(), "Text Files (*.txt);;All Files (*)");
     if(file_path.isEmpty()){
        return;
    }
    if(!add_document_file(file_path)){
        QMessageBox::warning(this, "Document Error", "Unable to add the selected document.");
    }
}

bool MainWindow::add_document_file(const QString& path){
//Opening a file dialog used to select a text file
    
    if(path.isEmpty()){
        return false;
    }
    if(!state_->can_edit_workspace()){
        return false;
    }
    //Retrieve the absolute file path and filename
    QFileInfo file_info(path);
    QString document_id = file_info.absoluteFilePath();
    QString document_title = file_info.fileName();
    //Opens the file selected
    QFile file(document_id);
    if(!file.open(QIODevice::ReadOnly)){
        return false;
    }
    //Reads entire file
    QByteArray file_data = file.readAll();
    if(file.error() != QFileDevice::NoError){
      return false;
    }
    file.close();
    //Constructign the document using the class from Milestone 2
    Document document(document_id.toUtf8().toStdString(), document_title.toUtf8().toStdString(), file_data.toStdString());
    try { //Add document to backend workspace
        workspace_.add_document(document);
        ++workspace_revision_;
        //Workspace change to invalidate the running build
        //build_worker_->cancel();
        //build_in_progress_ = false;
    } catch (const std::exception& error){
        return false;
    }
 state_->workspace_changed(*this); //notify current state of the workspace change
 //add document to GUI
 workspace_widget_->add_document(document_title);
 update_action_states();
 status_label_->setText(QString("Document added: %1").arg(document_title));
 return true;
}
void MainWindow::set_build_delay_ms(int ms_per_document){
    build_delay_ms_ = (ms_per_document >= 0) ? ms_per_document : 0;
}


void MainWindow::build_corpus() { //Rebuilt for Milestone 5
   state_->build_requested(*this);

}

void MainWindow::handle_build_result(BuildResult result){
   state_->build_finished(*this, std::move(result));
}

void MainWindow::document_selected(int row) {
    //Grab documents stored in Workspace
    const auto& docs = workspace_.documents();
    //Handling invalid selection
    if( row < 0 || static_cast<std::size_t>(row) >= docs.size()){
        workspace_widget_->clear_selected_document();
        return;
    }
    //Retrieving selected document
    const Document& document = docs[row];
    //Converting string to Qstring for the display
    QString title_qstring = QString::fromStdString(document.title());
    QString text_qstring = QString::fromStdString(document.text());
    workspace_widget_->show_selected_document(title_qstring, text_qstring);
}

void MainWindow::search_requested(const QString& query, int k) {
    //to ensure current corpus and valid query
    if(!state_->has_current_corpus()  || query.trimmed().isEmpty()){ 
        return;
    }
    //Converting QString to string
    std::string search_q = query.toUtf8().toStdString();
    //Performing the search using the backend from Milestone 2
    std::vector<SearchResult> results_vector = processing_core_.search(search_q, k);
    //displaying results in Search Widget
    search_widget_->set_results(std::move(results_vector));
    //More to do: clear the previous context display when the ContextWidget object is terminated
    context_widget_->clear_context();
    status_label_->setText("Search complete.");
}

void MainWindow::query_changed(const QString& query) {
    
    (void)query;  // To suppress unused-parameter warning.
    update_action_states(); //Updates the buttons based on the new query state
}

void MainWindow::build_context_requested(int token_budget) {
   //ensuring the corpus is current + query is valid
   if(!state_->has_current_corpus() || !search_widget_->has_query()){
    return;
   }
   //Retrieve the current search widget settings
   QString query = search_widget_->query();
   int k = search_widget_->result_count();
   //Converting the query into an string
   std::string query_string = query.toUtf8().toStdString();
   //Calling the Milestone 2 backend
   std::vector<ContextItem> items = processing_core_.build_context(query_string, k, static_cast<std::size_t>(token_budget));
   //displaying the returned context
   context_widget_->set_context(items);
   status_label_->setText("Context built successfully.");
}

void MainWindow::source_document_requested(const QString& document_id) {
    const auto& docs = workspace_.documents();
    for(std::size_t i = 0; i < docs.size(); ++i){
        QString current_id = QString::fromStdString(docs[i].id());
        if(current_id == document_id){
            //select document inside list
            workspace_widget_->select_document(static_cast<int>(i));
            document_selected(static_cast<int>(i));
            return;
        }
    }
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    if(!state_->has_current_corpus() || term.trimmed().isEmpty()){ //ensuring a valid corpus
        return;
    }
    //converting  the QString to string
    std::string term_string = term.trimmed().toUtf8().toStdString();
   //Document freq doesn't require a selected chunk
   std::size_t df = processing_core_.document_frequency(term_string);
   bool has_chunk = !chunk_id.isEmpty(); //determines whether a chunk has been selected
   std::size_t tf = 0;
   //Calculating the tf only when a chunk is available
   if(has_chunk){
    std::string chunk_str = chunk_id.toUtf8().toStdString();
    tf = processing_core_.term_frequency(term_string, chunk_str);

   }
   //displaying stats
   term_inspector_widget_->set_statistics(df, has_chunk, tf);
   status_label_->setText("Term Inspected.");


}

void MainWindow::reset_application() {
   state_->reset(*this);
}

void MainWindow::exit_application() {
    if(exit_callback_ != nullptr){
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    //The build corpus needs at least one doc
    bool docs_have = !workspace_.documents().empty();
    workspace_widget_->set_build_corpus_enabled(docs_have && state_->can_build());
    bool corpus_avail = state_->has_current_corpus();
    //Search needs current corpus and valid query
    bool search_avail = (corpus_avail && search_widget_->has_query());
    search_widget_->set_search_enabled(search_avail);
    context_widget_->set_build_context_enabled(search_avail);
    term_inspector_widget_->set_inspection_enabled(corpus_avail); //INspect needs current corpus
    

}

void MainWindow::invalidate_corpus() {
    //Invalidates the existing corpus whenever the workspace changes as previous 
    //results and stats might no longer match the current doc.
   workspace_widget_->set_corpus_status("Corpus: Not built");
    clear_processing_outputs();
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    //Clearing the results from the previous searches
    if(search_widget_ != nullptr){
        search_widget_->clear_results();
    }
    //Clearing selected chunk and term stats
    term_inspector_widget_ ->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();
    //clear context widget
    context_widget_->clear_context();
}

void MainWindow::closeEvent(QCloseEvent* event){
    //Used to stop the worker before exiting the application
    shutdown_worker();
    QMainWindow::closeEvent(event);

}
void MainWindow::set_fail_nxt_build_for_testing(bool enabled){
    fail_nxt_build_for_testing_ = enabled;
}
void MainWindow::handle_build_fail(const QString& reason){
  clear_processing_outputs();
  workspace_widget_->set_corpus_status("Corpus: Failed");
  status_label_->setText(QString("Corpus build failed: %1").arg(reason));
  update_action_states();
}
void MainWindow::shutdown_worker(){
   state_->exit_requested(*this);
}
void MainWindow::CorpusState::build_requested(MainWindow& window){
    if(window.workspace_.documents().empty()){
        return;
    }
    try{
        BuildRequest request{window.workspace_, window.workspace_revision_, window.fail_nxt_build_for_testing_, window.build_delay_ms_ };
        window.active_build_gen_ = window.build_worker_->submit(std::move(request));
        window.fail_nxt_build_for_testing_ = false;
    } catch (const std::exception& error){
        window.state_ = &window.failed_state_;
        window.clear_processing_outputs();
        window.workspace_widget_->set_corpus_status("Corpus: Failed");
        window.status_label_->setText(QString("Unable to start build: %1").arg(error.what()));
        window.update_action_states();
        return;
    }
    //transition to BuildingState
    window.state_ = &window.building_state_;
    window.clear_processing_outputs();
    window.workspace_widget_->set_corpus_status("Corpus: Building...");
    window.status_label_->setText("Building corpus in background...");
    window.update_action_states();
}
void MainWindow::CorpusState::workspace_changed(MainWindow& window){
    //cancelling work assoc. w/ previous Workspace
    window.build_worker_->cancel();
    window.state_ = &window.no_corpus_state_;  //transitioning to NoCorpusState
    window.invalidate_corpus();
}
void MainWindow::CorpusState::reset(MainWindow& window){
    window.build_worker_->cancel();
    window.workspace_.clear();
    ++window.workspace_revision_;
    //Returning to initial state
    window.state_ = &window.no_corpus_state_;
    Resettable* components[] = {window.workspace_widget_, window.search_widget_, window.term_inspector_widget_, window.context_widget_};
    for (Resettable* comp : components){
        comp->reset();
    //
    }
    window.status_label_->setText("Application reset.");
    window.update_action_states();
}
void MainWindow::CorpusState::exit_requested(MainWindow& window){
    window.state_ = &window.closing_state_;
    if(window.build_worker_){
        window.build_worker_->stop();
    }
}
void MainWindow::BuildingState::build_finished(MainWindow& window, BuildResult result){
    //Ignoring results from canceled / replaced requests
    if(result.gen != window.active_build_gen_){
        return;
    }
    #ifndef AIWS_FAULT_ACCEPT_STALE
    if(result.revision != window.workspace_revision_){
        window.state_ = &window.no_corpus_state_;
        window.invalidate_corpus();
        window.status_label_->setText("Workspace changed. Outdated corpus discarded.");
        return;
    }
    #endif
    //handling failed build
    if(!result.error.empty() || !result.core){
        #ifdef AIWS_FAULT_LOSE_FAILURE
        return;
        #else
        window.state_ = &window.failed_state_;
        if(!result.error.empty()){
            window.handle_build_fail(QString::fromStdString(result.error));
        } else {
            window.handle_build_fail("Build returned no corpus.");
        }
        return;
        #endif
    }
    //accepting the valid corpus by transferring the ownership
    window.processing_core_ = std::move(*result.core);
    //transitioning to ReadyState
    window.state_ = &window.ready_state_;
    window.clear_processing_outputs();
    QString chunk_status = QString("Corpus: %1 chunks").arg(static_cast<qulonglong>(window.processing_core_.chunk_count()));
    window.workspace_widget_->set_corpus_status(chunk_status);
    window.status_label_->setText("Corpus built successfully.");
    window.update_action_states();
}
}  // namespace aiws::gui
