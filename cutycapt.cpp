////////////////////////////////////////////////////////////////////
//
// CutyCapt - A Qt WebEngine Web Page Rendering Capture Utility
// (Qt6 modernization)
//
// Original: (c) 2003-2013 Bjoern Hoehrmann
//
////////////////////////////////////////////////////////////////////

#include "cutycapt.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QWebEngineCertificateError>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPageLayout>
#include <QSvgGenerator>
#include <QTextStream>
#include <QTimer>
#include <QWebEngineHttpRequest>
#if CUTYCAPT_SCRIPT
#include <QWebChannel>
#include <QWebEngineScript>
#endif
#include <cstdlib>
#include <cstring>
#include <iostream>

static struct _CutyExtMap {
	CutyCapt::OutputFormat id;
	const char* extension;
	const char* identifier;
} const CutyExtMap[] = {
	{ CutyCapt::SvgFormat, ".svg", "svg" },    { CutyCapt::PdfFormat, ".pdf", "pdf" },
	{ CutyCapt::PsFormat, ".ps", "ps" },       { CutyCapt::InnerTextFormat, ".txt", "itext" },
	{ CutyCapt::HtmlFormat, ".html", "html" }, { CutyCapt::JpegFormat, ".jpeg", "jpeg" },
	{ CutyCapt::PngFormat, ".png", "png" },    { CutyCapt::MngFormat, ".mng", "mng" },
	{ CutyCapt::TiffFormat, ".tiff", "tiff" }, { CutyCapt::GifFormat, ".gif", "gif" },
	{ CutyCapt::BmpFormat, ".bmp", "bmp" },    { CutyCapt::PpmFormat, ".ppm", "ppm" },
	{ CutyCapt::XbmFormat, ".xbm", "xbm" },    { CutyCapt::XpmFormat, ".xpm", "xpm" },
	{ CutyCapt::OtherFormat, "", "" }
};

////////////////////////////////////////////////////////////////////
// CutyEnginePage (Qt6-correct overrides live here)
////////////////////////////////////////////////////////////////////

CutyEnginePage::CutyEnginePage(QObject* parent) : QWebEnginePage(parent) {}

void CutyEnginePage::handleCertificateError(QWebEngineCertificateError error) {
	// If user didn't request insecure mode, do nothing (default rejection).
	if (!mInsecure)
		return;

	// Accept only overridable errors.
	if (error.isOverridable())
		error.acceptCertificate();
}

void CutyEnginePage::setUserAgent(const QString& userAgent) {
	mUserAgent = userAgent;
	profile()->setHttpUserAgent(userAgent);
}

void CutyEnginePage::setAlertString(const QString& alertString) {
	mAlertString = alertString;
}

QString CutyEnginePage::getAlertString() const {
	return mAlertString;
}

void CutyEnginePage::setPrintAlerts(bool printAlerts) {
	mPrintAlerts = printAlerts;
}

void CutyEnginePage::setCutyCapt(CutyCapt* cutyCapt) {
	mCutyCapt = cutyCapt;
}

void CutyEnginePage::setInsecure(bool insecure) {
	mInsecure = insecure;
}

QStringList CutyEnginePage::chooseFiles(FileSelectionMode /*mode*/, const QStringList& /*oldFiles*/,
                                       const QStringList& /*acceptedMimeTypes*/) {
	// Headless CLI tool: never open a file dialog.
	return {};
}

void CutyEnginePage::javaScriptAlert(const QUrl& /*securityOrigin*/, const QString& msg) {
	if (mPrintAlerts) {
		qDebug() << "[alert]" << msg;
	}

	// Preserve the "expect-alert triggers capture" behavior.
	if (!mAlertString.isEmpty() && msg == mAlertString && mCutyCapt) {
		QTimer::singleShot(10, mCutyCapt, SLOT(Delayed()));
	}
}

bool CutyEnginePage::javaScriptConfirm(const QUrl& /*securityOrigin*/, const QString& /*msg*/) {
	return true;
}

bool CutyEnginePage::javaScriptPrompt(const QUrl& /*securityOrigin*/, const QString& /*msg*/,
                                     const QString& /*defaultValue*/, QString* result) {
	if (result) *result = QString();
	return true;
}

////////////////////////////////////////////////////////////////////
// CutyPage
////////////////////////////////////////////////////////////////////

CutyPage::CutyPage(QWidget* parent) : QWebEngineView(parent) {
	mEnginePage = new CutyEnginePage(this);
	setPage(mEnginePage);

	// Qt6: TLS errors are delivered via signal; connect and optionally accept.
	QObject::connect(mEnginePage, &QWebEnginePage::certificateError,
	                 this, [this](QWebEngineCertificateError error) {
		                 mEnginePage->handleCertificateError(error);
	                 });
}

void CutyPage::setAttribute(QWebEngineSettings::WebAttribute option, const QString& value) {
	const bool on = (value == "on");
	const bool off = (value == "off");
	if (on || off)
		settings()->setAttribute(option, on);
}

void CutyPage::setAttribute(Qt::WidgetAttribute option, bool value) {
	QWidget::setAttribute(option, value);
}

void CutyPage::setUserAgent(const QString& userAgent) {
	mEnginePage->setUserAgent(userAgent);
}

void CutyPage::setAlertString(const QString& alertString) {
	mEnginePage->setAlertString(alertString);
}

QString CutyPage::getAlertString() const {
	return mEnginePage->getAlertString();
}

void CutyPage::setPrintAlerts(bool printAlerts) {
	mEnginePage->setPrintAlerts(printAlerts);
}

void CutyPage::setCutyCapt(CutyCapt* cutyCapt) {
	mEnginePage->setCutyCapt(cutyCapt);
}

void CutyPage::setInsecure(bool insecure) {
	mEnginePage->setInsecure(insecure);
}

#if CUTYCAPT_SCRIPT
// Install a WebChannel bridge object into the JS environment as window[scriptObjectName]
// and inject user script source at DocumentReady.
void CutyPage::installScriptSupport(const QString& scriptObjectName,
                                    const QString& injectedUserScriptSource,
                                    bool silent) {
	if (!mChannel) {
		mChannel = new QWebChannel(this);
		mBridge = new CutyScriptBridge(this);

		// Register the object under a stable internal name.
		// We then alias it to window[scriptObjectName] in JS.
		mChannel->registerObject(QStringLiteral("cuty"), mBridge);
		page()->setWebChannel(mChannel);

		// Optional: let scripts print to stderr via cuty.jsLog("...")
		QObject::connect(mBridge, &CutyScriptBridge::log, this, [silent](const QString& msg) {
			if (!silent)
				std::clog << "[script] " << msg.toStdString() << std::endl;
		});
	}

	const QString objName = scriptObjectName.isEmpty() ? QStringLiteral("cuty") : scriptObjectName;

	// 1) Bootstrap: load qwebchannel.js and expose bridge as window[objName]
	const QString bootstrap = QStringLiteral(R"(
		(function() {
			function boot() {
				if (typeof QWebChannel === 'undefined') {
					// qwebchannel.js not loaded yet
					return false;
				}
				new QWebChannel(qt.webChannelTransport, function(channel) {
					window.%1 = channel.objects.cuty;
				});
				return true;
			}

			// If already loaded, just boot
			if (typeof qt !== 'undefined' && qt.webChannelTransport) {
				if (boot()) return;
			}

			// Load qwebchannel.js from Qt resource
			var s = document.createElement('script');
			s.src = 'qrc:///qtwebchannel/qwebchannel.js';
			s.onload = function() { boot(); };
			(document.head || document.documentElement).appendChild(s);
		})();
	)").arg(objName);

	QWebEngineScript bootScript;
	bootScript.setName(QStringLiteral("cutycapt-webchannel-bootstrap"));
	bootScript.setInjectionPoint(QWebEngineScript::DocumentReady);
	bootScript.setRunsOnSubFrames(true);
	bootScript.setWorldId(QWebEngineScript::MainWorld);
	bootScript.setSourceCode(bootstrap);
	page()->scripts().insert(bootScript);

	// 2) Inject user script (if provided)
	if (!injectedUserScriptSource.trimmed().isEmpty()) {
		QWebEngineScript userScript;
		userScript.setName(QStringLiteral("cutycapt-user-injected-script"));
		userScript.setInjectionPoint(QWebEngineScript::DocumentReady);
		userScript.setRunsOnSubFrames(true);
		userScript.setWorldId(QWebEngineScript::MainWorld);
		userScript.setSourceCode(injectedUserScriptSource);
		page()->scripts().insert(userScript);
	}
}
#endif

////////////////////////////////////////////////////////////////////
// CutyCapt
////////////////////////////////////////////////////////////////////

CutyCapt::CutyCapt(CutyPage* page, const QString& output, int delay, OutputFormat format,
                   const QString& scriptProp, const QString& scriptCode, bool insecure, bool smooth,
                   bool silent)
	: mOutput(output),
	  mDelay(delay),
	  mPage(page),
	  mFormat(format),
	  mScriptObj(new QObject()),
	  mScriptProp(scriptProp),
	  mScriptCode(scriptCode),
	  mInsecure(insecure),
	  mSmooth(smooth),
	  mSilent(silent) {
	mPage->setCutyCapt(this);
	mPage->setInsecure(insecure);

#if CUTYCAPT_SCRIPT
	wireScriptSignals();
#endif
}

#if CUTYCAPT_SCRIPT
void CutyCapt::wireScriptSignals() {
	// Optional convenience: if a script calls cuty.jsDone("tag"), you can treat it like expect-alert.
	// We keep it conservative: only auto-capture if user configured expect-alert and the tag matches.
	// The main “expect-alert” mechanism remains javaScriptAlert() (classic behavior).
	// (Nothing required here right now; kept as a hook for future.)
}
#endif

void CutyCapt::DocumentComplete(bool ok) {
	if (!mSilent && !ok) {
		std::cerr << "WebEngine failed to completely load url" << std::endl;
		QApplication::exit(1);
		return;
	} else if (!mSilent) {
		std::cerr << "WebEngine finished loadFinished(true)" << std::endl;
	}

	mSawDocumentComplete = true;

	// Make viewport sizing more reliable in Qt6: ask DOM for scroll size.
	updateViewportToContentThenMaybeCapture();
}

void CutyCapt::updateViewportToContentThenMaybeCapture() {
	// If the caller expects an alert trigger, we keep waiting.
	if (!mPage->getAlertString().isEmpty())
		return;

	const QString js = R"(
		(function() {
			const de = document.documentElement;
			const body = document.body;
			const w = Math.max(de ? de.scrollWidth : 0, body ? body.scrollWidth : 0, window.innerWidth || 0);
			const h = Math.max(de ? de.scrollHeight : 0, body ? body.scrollHeight : 0, window.innerHeight || 0);
			return { width: w, height: h };
		})()
	)";

	mPage->page()->runJavaScript(js, [this](const QVariant& v) {
		const auto m = v.toMap();
		const int w = m.value("width").toInt();
		const int h = m.value("height").toInt();

		if (w > 0 && h > 0) {
			mViewSize = QSize(w, h);
			mPage->setMinimumSize(mViewSize);
			mPage->resize(mViewSize);
			mSawGeometryChange = true;
		} else {
			// Fall back to current widget size if DOM size is not available yet.
			mViewSize = mPage->size();
			mSawGeometryChange = !mViewSize.isEmpty();
		}

		if (mSawDocumentComplete && mSawGeometryChange)
			TryDelayedRender();
	});
}

void CutyCapt::TryDelayedRender() {
	if (!mPage->getAlertString().isEmpty())
		return;

	if (mDelay > 0) {
		QTimer::singleShot(mDelay, this, SLOT(Delayed()));
		return;
	}

	saveSnapshot();
}

void CutyCapt::Timeout() {
	if (!mSilent)
		std::clog << "Timeout reached" << std::endl;

	saveSnapshot();
}

void CutyCapt::Delayed() {
	saveSnapshot();
}

void CutyCapt::onContentsSizeChanged(const QSizeF& size) {
	// Still useful as an extra hint; but JS sizing is the primary approach now.
	if (!mSilent) {
		std::clog << "contentsSizeChanged (" << size.width() << ", " << size.height() << ")"
		          << std::endl;
	}
	if (size.width() > 0 && size.height() > 0) {
		mViewSize = size.toSize();
		mSawGeometryChange = true;
	}
}

void CutyCapt::pdfPrintFinish(const QString& file, bool success) {
	if (!success && !mSilent) {
		std::cerr << "Failed to print page to PDF '" << file.toStdString() << "'" << std::endl;
		QApplication::exit(1);
		return;
	}
	QApplication::quit();
}

void CutyCapt::saveSnapshot() {
	QPainter painter;
	const char* format = nullptr;

	for (int ix = 0; CutyExtMap[ix].id != OtherFormat; ++ix) {
		if (CutyExtMap[ix].id == mFormat) {
			format = CutyExtMap[ix].identifier;
			break;
		}
	}

	QString out = mOutput;
	mTimeoutTimer.stop();

	// Make sure we have some non-zero size.
	if (mViewSize.isEmpty())
		mViewSize = mPage->size();
	if (mViewSize.isEmpty())
		mViewSize = QSize(800, 600);

	switch (mFormat) {
		case SvgFormat: {
			QSvgGenerator svg;
			svg.setFileName(out);
			svg.setSize(mViewSize);
			painter.begin(&svg);
			mPage->render(&painter);
			painter.end();
			QApplication::quit();
			break;
		}
		case PdfFormat:
		case PsFormat: {
			// Qt6: async; observe QWebEnginePage::pdfPrintingFinished.
			mPage->page()->printToPdf(out);
			break;
		}
		case InnerTextFormat: {
			mPage->page()->toPlainText([out](const QString& result) {
				QFile file(out);
				if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
					QTextStream s(&file);
					s.setEncoding(QStringConverter::Utf8);
					s << result;
				}
				QApplication::quit();
			});
			break;
		}
		case HtmlFormat: {
			mPage->page()->toHtml([out](const QString& result) {
				QFile file(out);
				if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
					QTextStream s(&file);
					s.setEncoding(QStringConverter::Utf8);
					s << result;
				}
				QApplication::quit();
			});
			break;
		}
		default: {
			// Prefer grab() for QWidget-backed rendering.
			// (render() can sometimes race WebEngine painting depending on platform)
			QPixmap px = mPage->grab();

			// If grab fails for any reason, fall back to render into QImage.
			if (px.isNull()) {
				QImage image(mViewSize, QImage::Format_ARGB32);
				image.fill(Qt::transparent);
				painter.begin(&image);
				if (mSmooth) {
					painter.setRenderHint(QPainter::SmoothPixmapTransform);
					painter.setRenderHint(QPainter::Antialiasing);
					painter.setRenderHint(QPainter::TextAntialiasing);
				}
				mPage->render(&painter);
				painter.end();
				image.save(out, format);
			} else {
				px.save(out, format);
			}

			QApplication::quit();
		}
	}
}

////////////////////////////////////////////////////////////////////
// CLI / main
////////////////////////////////////////////////////////////////////

static void CaptHelp(const char* argv0) {
	const QString prog = QFileInfo(QString::fromLocal8Bit(argv0)).fileName();

    printf("%s",
           " ----------------------------------------------------------------------------------\n");
    printf(" Usage: %s --url=http://www.example.org/ --out=localfile.png\n",
           prog.toLocal8Bit().constData());
	printf("%s",
	       " ----------------------------------------------------------------------------------\n"
	       "  --help                             Print this help page and exit                 \n"
	       "  --url=<url>                        The URL to capture (http:...|file:...|...)    \n"
	       "  --out=<path>                       The target file (.png|pdf|svg|jpeg|...)       \n"
	       "  --out-format=<f>                   Like extension in --out, overrides heuristic  \n"
	       "  --min-width=<int>                  Minimal width for the image (default: 800)    \n"
	       "  --min-height=<int>                 Minimal height for the image (default: 600)   \n"
	       "  --max-wait=<ms>                    Don't wait more than (default: 90000, inf: 0) \n"
	       "  --delay=<ms>                       After successful load, wait (default: 0)      \n"
	       "  --header=<name>:<value>            request header; repeatable; some can't be set \n"
	       "  --body-string=<string>             Unencoded request body (default: none)        \n"
	       "  --body-base64=<base64>             Base64-encoded request body (default: none)   \n"
	       "  --app-name=<name>                  appName used in User-Agent; default is none   \n"
	       "  --app-version=<version>            appVers used in User-Agent; default is none   \n"
	       "  --user-agent=<string>              Override the User-Agent header Qt would set   \n"
	       "  --javascript=<on|off>              JavaScript execution (default: on)            \n"
	       "  --plugins=<on|off>                 Plugin execution (default: unknown)           \n"
	       "  --auto-load-images=<on|off>        Automatic image loading (default: on)         \n"
	       "  --js-can-open-windows=<on|off>     Script can open windows? (default: unknown)   \n"
	       "  --js-can-access-clipboard=<on|off> Script clipboard privs (default: unknown)     \n"
	       "  --print-backgrounds=<on|off>       Backgrounds in PDF output (default: off)      \n"
	       "  --zoom-factor=<float>              Page zoom factor (default: no zooming)        \n"
	       "  --smooth                           Enable higher-quality painter hints           \n"
	       "  --insecure                         Ignore SSL/TLS certificate errors (overridable)\n"
	       "  --silent                           Less console output                           \n"
#if CUTYCAPT_SCRIPT
	       "  --inject-script=<path>             JavaScript injected at DocumentReady           \n"
	       "  --script-object=<string>           window[<string>] becomes the WebChannel bridge\n"
	       "  --expect-alert=<string>            Capture when alert(<string>) occurs            \n"
	       "  --debug-print-alerts               Print JS alert(...) strings                    \n"
#endif
	       " ----------------------------------------------------------------------------------\n"
	       "  <f> is svg,pdf,ps,itext,html,png,jpeg,mng,tiff,gif,bmp,ppm,xbm,xpm               \n"
	       " ----------------------------------------------------------------------------------\n");
}

int main(int argc, char* argv[]) {
	bool argHelp = false;
	int argDelay = 0;
	bool argSilent = false;
	bool argInsecure = false;
	int32_t argMinWidth = 800;
	int32_t argMinHeight = 600;
	uint32_t argMaxWait = 90000;
	bool argSmooth = false;

	const char* argUrl = nullptr;
	QString argOut;

#if CUTYCAPT_SCRIPT
	const char* argInjectScript = nullptr;
	const char* argScriptObject = nullptr;
#endif

	CutyCapt::OutputFormat format = CutyCapt::OtherFormat;

	QApplication app(argc, argv);

	CutyPage page;

	QByteArray body;
	QWebEngineHttpRequest req{};

	for (int ax = 1; ax < argc; ++ax) {
		const char* s = argv[ax];
		const char* value = nullptr;

		if (strcmp("--silent", s) == 0) {
			argSilent = true;
			continue;
		} else if (strcmp("--help", s) == 0) {
			argHelp = true;
			break;
		} else if (strcmp("--insecure", s) == 0) {
			argInsecure = true;
			continue;
		} else if (strcmp("--smooth", s) == 0) {
			argSmooth = true;
			continue;
#if CUTYCAPT_SCRIPT
		} else if (strcmp("--debug-print-alerts", s) == 0) {
			page.setPrintAlerts(true);
			continue;
#endif
		}

		value = strchr(s, '=');
		if (!value) {
			argHelp = true;
			break;
		}

		size_t nlen = size_t(value++ - s);

		if (strncmp("--url", s, nlen) == 0) {
			argUrl = value;
		} else if (strncmp("--min-width", s, nlen) == 0) {
			argMinWidth = strtol(value, nullptr, 0);
		} else if (strncmp("--min-height", s, nlen) == 0) {
			argMinHeight = strtol(value, nullptr, 0);
		} else if (strncmp("--delay", s, nlen) == 0) {
			argDelay = strtol(value, nullptr, 0);
		} else if (strncmp("--max-wait", s, nlen) == 0) {
			argMaxWait = strtol(value, nullptr, 0);
		} else if (strncmp("--out", s, nlen) == 0) {
			argOut = value;
			if (format == CutyCapt::OtherFormat) {
				for (int ix = 0; CutyExtMap[ix].id != CutyCapt::OtherFormat; ++ix) {
					if (argOut.endsWith(CutyExtMap[ix].extension))
						format = CutyExtMap[ix].id;
				}
			}
		} else if (strncmp("--auto-load-images", s, nlen) == 0) {
			page.setAttribute(QWebEngineSettings::AutoLoadImages, value);
		} else if (strncmp("--javascript", s, nlen) == 0) {
			page.setAttribute(QWebEngineSettings::JavascriptEnabled, value);
		} else if (strncmp("--plugins", s, nlen) == 0) {
			page.setAttribute(QWebEngineSettings::PluginsEnabled, value);
		} else if (strncmp("--js-can-open-windows", s, nlen) == 0) {
			page.setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, value);
		} else if (strncmp("--js-can-access-clipboard", s, nlen) == 0) {
			page.setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, value);
		} else if (strncmp("--print-backgrounds", s, nlen) == 0) {
			page.setAttribute(QWebEngineSettings::PrintElementBackgrounds, value);
		} else if (strncmp("--zoom-factor", s, nlen) == 0) {
			page.setZoomFactor(static_cast<qreal>(QString(value).toFloat()));
		} else if (strncmp("--app-name", s, nlen) == 0) {
			app.setApplicationName(value);
		} else if (strncmp("--app-version", s, nlen) == 0) {
			app.setApplicationVersion(value);
		} else if (strncmp("--body-base64", s, nlen) == 0) {
			body = QByteArray::fromBase64(value);
		} else if (strncmp("--body-string", s, nlen) == 0) {
			body = QByteArray(value);
		} else if (strncmp("--user-agent", s, nlen) == 0) {
			page.setUserAgent(value);
		} else if (strncmp("--out-format", s, nlen) == 0) {
			for (int ix = 0; CutyExtMap[ix].id != CutyCapt::OtherFormat; ++ix) {
				if (strcmp(value, CutyExtMap[ix].identifier) == 0)
					format = CutyExtMap[ix].id;
			}
			if (format == CutyCapt::OtherFormat) {
				argHelp = true;
				break;
			}
		} else if (strncmp("--header", s, nlen) == 0) {
			const char* hv = strchr(value, ':');
			if (!hv) {
				argHelp = true;
				break;
			}
			req.setHeader(QByteArray(value, hv - value), QByteArray(hv + 1));
#if CUTYCAPT_SCRIPT
		} else if (strncmp("--inject-script", s, nlen) == 0) {
			argInjectScript = value;
		} else if (strncmp("--script-object", s, nlen) == 0) {
			argScriptObject = value;
		} else if (strncmp("--expect-alert", s, nlen) == 0) {
			page.setAlertString(value);
#endif
		} else {
			argHelp = true;
		}
	}

	if (!argUrl || argOut.isEmpty() || argHelp) {
		CaptHelp(argv[0]);
		return EXIT_FAILURE;
	}

	req.setUrl(QUrl::fromEncoded(argUrl));

	if (!body.isNull())
		req.setPostData(body);

#if CUTYCAPT_SCRIPT
	// Load injected user script file if provided
	QString scriptProp(argScriptObject ? argScriptObject : "");
	QString scriptCode;
	if (argInjectScript) {
		QFile file(argInjectScript);
		if (file.open(QIODevice::ReadOnly)) {
			QTextStream stream(&file);
			stream.setEncoding(QStringConverter::Utf8);
			scriptCode = stream.readAll();
			file.close();
		}
	}

	// Install WebChannel + inject scripts *before* page.load()
	// so injection occurs at DocumentReady on the first navigation.
	page.installScriptSupport(scriptProp, scriptCode, argSilent);
#else
	QString scriptProp;
	QString scriptCode;
#endif

	CutyCapt main(&page, argOut, argDelay, format, QString{}, QString{}, argInsecure, argSmooth,
	              argSilent);

	QObject::connect(&page, &QWebEngineView::loadFinished, &main, &CutyCapt::DocumentComplete);
	QObject::connect(page.page(), &QWebEnginePage::contentsSizeChanged, &main,
	                 &CutyCapt::onContentsSizeChanged);

	// Qt6 docs: observe pdfPrintingFinished for printToPdf completion.
	QObject::connect(page.page(), &QWebEnginePage::pdfPrintingFinished, &main,
	                 &CutyCapt::pdfPrintFinish);

	if (argMaxWait > 0) {
		QTimer& timer = main.mTimeoutTimer;
		timer.setInterval(int(argMaxWait));
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &main, &CutyCapt::Timeout);
		timer.start();
	}

	page.setAttribute(QWebEngineSettings::WebAttribute::ShowScrollBars, "off");
	page.setAttribute(Qt::WA_DontShowOnScreen, true);

	QSize argSize(argMinWidth, argMinHeight);
	page.setMinimumSize(argSize);
	page.setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
	page.resize(argSize);
	page.show();

	page.load(req);

	return app.exec();
}