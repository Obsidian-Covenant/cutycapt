#pragma once

#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>

// Enable script injection / remote control
#ifndef CUTYCAPT_SCRIPT
#define CUTYCAPT_SCRIPT 1
#endif

#if CUTYCAPT_SCRIPT
#include <QWebChannel>
#include <QWebEngineScript>
#endif

class CutyCapt;

// Modern WebEngine hooks belong on QWebEnginePage, not QWebEngineView.
class CutyEnginePage : public QWebEnginePage {
	Q_OBJECT
public:
	explicit CutyEnginePage(QObject* parent = nullptr);

	void setUserAgent(const QString& userAgent);
	void setAlertString(const QString& alertString);
	QString getAlertString() const;
	void setPrintAlerts(bool printAlerts);
	void setCutyCapt(CutyCapt* cutyCapt);
	void setInsecure(bool insecure);

	// Qt6: handle TLS errors via signal rather than overriding a virtual
	void handleCertificateError(/* by value to allow accepting */ class QWebEngineCertificateError error);

protected:
	// File chooser
	QStringList chooseFiles(FileSelectionMode mode, const QStringList& oldFiles,
	                        const QStringList& acceptedMimeTypes) override;

	// JS dialogs
	void javaScriptAlert(const QUrl& securityOrigin, const QString& msg) override;
	bool javaScriptConfirm(const QUrl& securityOrigin, const QString& msg) override;
	bool javaScriptPrompt(const QUrl& securityOrigin, const QString& msg, const QString& defaultValue,
	                      QString* result) override;

private:
	QString mUserAgent;
	QString mAlertString;
	bool mPrintAlerts{ false };
	bool mInsecure{ false };
	CutyCapt* mCutyCapt{ nullptr };
};

#if CUTYCAPT_SCRIPT
// Object exposed to JS via QWebChannel.
// It is intentionally tiny; your injected script can store state on it.
class CutyScriptBridge : public QObject {
	Q_OBJECT
public:
	explicit CutyScriptBridge(QObject* parent = nullptr) : QObject(parent) {}

signals:
	void log(const QString& msg);
	void done(const QString& tag); // optional convenience if your scripts want it

public slots:
	void jsLog(const QString& msg) { emit log(msg); }
	void jsDone(const QString& tag) { emit done(tag); }
};
#endif

class CutyPage : public QWebEngineView {
	Q_OBJECT
public:
	explicit CutyPage(QWidget* parent = nullptr);

	void setAttribute(QWebEngineSettings::WebAttribute option, const QString& value);
	void setAttribute(Qt::WidgetAttribute option, bool value);

	// Forwarders to the actual engine page
	void setUserAgent(const QString& userAgent);
	void setAlertString(const QString& alertString);
	QString getAlertString() const;
	void setPrintAlerts(bool printAlerts);
	void setCutyCapt(CutyCapt* cutyCapt);
	void setInsecure(bool insecure);

#if CUTYCAPT_SCRIPT
	// Script support
	void installScriptSupport(const QString& scriptObjectName,
	                          const QString& injectedUserScriptSource,
	                          bool silent);
#endif

private:
	CutyEnginePage* mEnginePage{ nullptr };

#if CUTYCAPT_SCRIPT
	QWebChannel* mChannel{ nullptr };
	CutyScriptBridge* mBridge{ nullptr };
#endif
};

class CutyCapt : public QObject {
	Q_OBJECT

public:
	enum OutputFormat {
		SvgFormat,
		PdfFormat,
		PsFormat,
		InnerTextFormat,
		HtmlFormat,
		PngFormat,
		JpegFormat,
		MngFormat,
		TiffFormat,
		GifFormat,
		BmpFormat,
		PpmFormat,
		XbmFormat,
		XpmFormat,
		OtherFormat
	};

	CutyCapt(CutyPage* page, const QString& output, int delay, OutputFormat format,
	         const QString& scriptProp, const QString& scriptCode, bool insecure, bool smooth,
	         bool silent);

public slots:
	void Timeout();
	void pdfPrintFinish(const QString& filePath, bool success);
	void DocumentComplete(bool ok);
	void onContentsSizeChanged(const QSizeF& size);

private slots:
	void Delayed();

private:
	void TryDelayedRender();
	void saveSnapshot();
	void updateViewportToContentThenMaybeCapture();

#if CUTYCAPT_SCRIPT
	void wireScriptSignals();
#endif

private:
	QString mOutput;
	int mDelay{ 0 };
	CutyPage* mPage{ nullptr };
	OutputFormat mFormat{ OtherFormat };
	QObject* mScriptObj{ nullptr };
	QString mScriptProp;
	QString mScriptCode;

	bool mSawDocumentComplete{ false };
	bool mSawGeometryChange{ false };

	QSize mViewSize;
	bool mInsecure{ false };
	bool mSmooth{ false };
	bool mSilent{ false };

public:
	QTimer mTimeoutTimer;
};