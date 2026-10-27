// Diálogo "Agent (MCP)": activar el control por agente, ver su estado y preparar la conexión con el agente (una vez).
// Cómo funciona: KoniSynth abre un servidor local (Agent/AgentServer.h, solo 127.0.0.1, primer puerto libre de 47800-47899) y
// anuncia puerto y token en %APPDATA%/KoniSynth/agent. El puente MCP (Agent/KoniSynth-mcp/konisynth_mcp.py, Python) lo arranca
// el propio agente cuando está registrado en él; el usuario no lanza nada a mano. Se incluye desde KoniSynth.cpp (mismo TU).
// Instalación: el comando que se copia (una línea de Python, igual en cmd, PowerShell y bash) descarga el puente de la
// etiqueta de esta versión (v + KONI_VERSION_LABEL) del repositorio público a la carpeta de datos del usuario
// (%APPDATA%\KoniSynth\KoniSynth-mcp, ~/.config/KoniSynth/KoniSynth-mcp en Linux), crea su .venv e instala sus paquetes.
// El plugin no abre ninguna conexión hacia fuera: descarga quien ejecuta el comando.
#pragma once

struct AgentPanel : juce::Component, private juce::Timer
{
    explicit AgentPanel (KoniSynthProcessor& p) : koni (p)
    {
        enable.setButtonText ("Agent control");
        enable.setTooltip ("Opens a local control server (127.0.0.1 only, port chosen automatically in 47800-47899) so an AI agent "
                           "can read and set parameters, browse and save presets and listen. Off: no port is open.");
        enable.setToggleState (koni.isAgentEnabled(), juce::dontSendNotification);
        enable.onClick = [this]
        {
            if (! koni.setAgentEnabled (enable.getToggleState()))
            {
                enable.setToggleState (false, juce::dontSendNotification);
                status.setText ("Could not start: no free local port (47800-47899) or the announce file could not be written.",
                                juce::dontSendNotification);
            }
            refresh();
        };
        addAndMakeVisible (enable);
        for (auto* l : { &status, &steps, &bridgeInfo })
        {
            l->setJustificationType (juce::Justification::topLeft);
            l->setFont (juce::FontOptions (16.0f));
            addAndMakeVisible (*l);
        }
        steps.setText ("Once: 1) Copy install command and run it in a terminal: it downloads the bridge of this version from "
                       "GitHub to your user folder and installs its Python packages. 2) Register it in your agent (Register in "
                       "agent...: a command for Claude Code or Codex, or the JSON entry for other MCP clients). Then, every time: "
                       "turn Agent control on here and ask your agent to connect. The agent starts the bridge itself.",
                       juce::dontSendNotification);
        steps.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.8f));
        install.setButtonText ("Copy install command");
        install.setTooltip ("One Python command: downloads the bridge of this KoniSynth version from the public repository (tag "
                            + releaseTag() + ") to " + downloadDir().getFullPathName() + ", creates its Python environment (.venv) "
                            "and installs its pinned packages (mcp, numpy). If the bridge is already there (for example the "
                            "KoniSynth-mcp folder of the release zip copied by hand), next to KoniSynth or chosen with Locate, it "
                            "downloads nothing and only creates the environment. Run it again after updating KoniSynth.");
        install.onClick = [this] { copy (installCommand(), "Install command copied: paste it in a terminal."); };
        reg.setButtonText ("Register in agent...");
        reg.setTooltip ("Copies what registers the bridge in your agent, for every project: the command for Claude Code or Codex "
                        "(run it in a terminal), or the JSON entry for other MCP clients (Claude Desktop, Cursor, VS Code...: add it "
                        "to their mcpServers configuration).");
        reg.onClick = [this] { showRegisterMenu(); };
        locate.setButtonText ("Locate bridge...");
        locate.setTooltip ("Optional: use a bridge in another folder instead of downloading it (for example the KoniSynth-mcp folder "
                           "of the release zip, offline). Choose its konisynth_mcp.py.");
        locate.onClick = [this] { chooseBridge(); };
        for (auto* b : { &install, &reg, &locate })
            addAndMakeVisible (*b);
        refresh();
        startTimerHz (2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        auto top = r.removeFromTop (30);
        enable.setBounds (top.removeFromLeft (180));
        status.setBounds (top.withTrimmedTop (4));
        r.removeFromTop (12);
        steps.setBounds (r.removeFromTop (92));
        r.removeFromTop (8);
        bridgeInfo.setBounds (r.removeFromTop (64));
        r.removeFromTop (12);
        auto buttons = r.removeFromTop (34);
        install.setBounds (buttons.removeFromLeft (230));
        buttons.removeFromLeft (10);
        reg.setBounds (buttons.removeFromLeft (250));
        buttons.removeFromLeft (10);
        locate.setBounds (buttons.removeFromLeft (180));
    }

    // Carpeta de descarga del puente: %APPDATA%\KoniSynth\KoniSynth-mcp (Windows), ~/.config/KoniSynth/KoniSynth-mcp (Linux)
    static juce::File downloadDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KoniSynth/KoniSynth-mcp");
    }

    // Etiqueta de la release de esta versión en el repositorio (las releases de main se etiquetan v<versión>)
    static juce::String releaseTag()
    {
       #ifdef KONI_VERSION_LABEL
        return "v" + juce::String (KONI_VERSION_LABEL);
       #else
        return "main";
       #endif
    }

private:
    // Rutas del entorno de Python según la plataforma (venv: Scripts\python.exe en Windows, bin/python en Linux y macOS)
   #if JUCE_WINDOWS
    static constexpr const char* venvPython = ".venv/Scripts/python.exe", * pythonExe = "python.exe", * pathSep = ";", * systemPython = "python";
   #else
    static constexpr const char* venvPython = ".venv/bin/python", * pythonExe = "python3", * pathSep = ":", * systemPython = "python3";
   #endif

    static juce::File rememberFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KoniSynth/mcp-bridge.txt");
    }

    // Puente que ya está en disco y se usa donde está, por orden: el elegido con Locate (recordado), el del repositorio (builds
    // de desarrollo), el de la carpeta de datos (descargado o copiado a mano del zip) y el que va junto al ejecutable o al
    // plugin (carpeta del zip de la release). Un VST3 instalado en la carpeta del sistema no lo tiene al lado: si tampoco está en
    // la carpeta de datos, se descarga
    static juce::File localScript()
    {
        if (const auto saved = juce::File (rememberFile().loadFileAsString().trim()); saved.existsAsFile())
            return saved;
       #ifdef KONI_REPO_DIR
        if (const auto repo = juce::File (KONI_REPO_DIR).getChildFile ("Agent/KoniSynth-mcp/konisynth_mcp.py"); repo.existsAsFile())
            return repo;
       #endif
        if (const auto data = downloadDir().getChildFile ("konisynth_mcp.py"); data.existsAsFile())
            return data;
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (auto dir = self.getParentDirectory(); dir.exists() && ! dir.isRoot(); dir = dir.getParentDirectory())
            if (const auto f = dir.getChildFile ("KoniSynth-mcp").getChildFile ("konisynth_mcp.py"); f.existsAsFile())
                return f;
        return {};
    }

    // Versión del puente descargado (version.txt, la escribe el comando de instalación al terminar; uno copiado a mano no la lleva)
    static juce::String downloadedTag() { return downloadDir().getChildFile ("version.txt").loadFileAsString().trim(); }

    // El puente de la carpeta de datos es de otra versión de KoniSynth (descargado antes de actualizar)
    static bool downloadOutdated()
    {
        const auto tag = downloadedTag();
        return localScript() == downloadDir().getChildFile ("konisynth_mcp.py") && tag.isNotEmpty() && tag != releaseTag();
    }

    // El comando de instalación descarga si no hay puente en disco o si el de la carpeta de datos es de otra versión
    static bool needsDownload() { return ! localScript().existsAsFile() || downloadOutdated(); }

    static bool pythonInPath()
    {
        for (const auto& dir : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), pathSep, "\""))
            if (dir.isNotEmpty() && juce::File (dir.trim()).getChildFile (pythonExe).existsAsFile())
                return true;
        return false;
    }

    static juce::String quote (const juce::String& s) { return "\"" + s + "\""; }

    // Puente al que se refieren los comandos: el que ya está en disco; si no hay ninguno, el de la carpeta de datos (el comando
    // de instalación lo descarga ahí)
    static juce::File targetScript()
    {
        const auto local = localScript();
        return local.existsAsFile() ? local : downloadDir().getChildFile ("konisynth_mcp.py");
    }

    // Una sola orden de Python, igual en cmd, PowerShell y bash (sin $, ! ni comillas dobles dentro; carpeta y etiqueta como
    // argumentos): con etiqueta, descarga los ficheros del puente de esa etiqueta por HTTPS; después crea el .venv (o lo
    // reutiliza), instala requirements.txt con su pip y, si descargó, anota la versión en version.txt
    juce::String installCommand() const
    {
        const juce::String code =
            "import os,sys,venv,subprocess as s,urllib.request as u;d,t=sys.argv[1:3];os.makedirs(d,exist_ok=True);"
            "b='https://raw.githubusercontent.com/k0n1m4k1/KoniSynth/'+t+'/Agent/KoniSynth-mcp/';"
            "[u.urlretrieve(b+f,os.path.join(d,f)) for f in ('konisynth_mcp.py','analysis.py','generator.py','requirements.txt','README.md') if t!='-'];"
            "venv.create(os.path.join(d,'.venv'),with_pip=True);"
            "s.check_call([os.path.join(d,'.venv','Scripts' if os.name=='nt' else 'bin','python'),'-m','pip','install','-r',os.path.join(d,'requirements.txt')]);"
            "t!='-' and open(os.path.join(d,'version.txt'),'w').write(t);print('KoniSynth bridge ready in',d)";
        return juce::String (systemPython) + " -c " + quote (code) + " " + quote (targetScript().getParentDirectory().getFullPathName())
               + " " + (needsDownload() ? releaseTag() : juce::String ("-"));
    }

    // Registro del puente en el agente: el python de su .venv (el que crea la instalación) y el script. Sintaxis comprobada con
    // las CLI (2026-10-27): claude mcp add <nombre> [--scope user] -- <orden> <args> (Claude Code); codex mcp add <nombre> --
    // <orden>... (Codex CLI 0.147, configuración de usuario en ~/.codex/config.toml). Los demás clientes (Claude Desktop,
    // Cursor, VS Code...) leen una entrada mcpServers en JSON con command y args
    enum class Client { claudeCode, codex, json };
    juce::String registerText (Client c) const
    {
        const auto script = targetScript();
        const auto python = script.getParentDirectory().getChildFile (venvPython).getFullPathName();
        if (c == Client::json)
        {
            auto* server = new juce::DynamicObject();
            server->setProperty ("command", python);
            server->setProperty ("args", juce::Array<juce::var> { script.getFullPathName() });
            auto* servers = new juce::DynamicObject();
            servers->setProperty ("KoniSynth", juce::var (server));
            auto* root = new juce::DynamicObject();
            root->setProperty ("mcpServers", juce::var (servers));
            return juce::JSON::toString (juce::var (root)); // escapa las barras invertidas de Windows
        }
        return (c == Client::codex ? juce::String ("codex mcp add KoniSynth -- ") : juce::String ("claude mcp add KoniSynth --scope user -- "))
               + quote (python) + " " + quote (script.getFullPathName());
    }

    void showRegisterMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Claude Code (command)");
        m.addItem (2, "Codex CLI (command)");
        m.addItem (3, "Other MCP clients (JSON entry)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (reg), [safe = juce::Component::SafePointer<AgentPanel> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 3)
                safe->copy (safe->registerText (Client::json), "JSON entry copied: add it to your client's MCP servers configuration.");
            else
                safe->copy (safe->registerText (r == 2 ? Client::codex : Client::claudeCode),
                            juce::String (r == 2 ? "Codex" : "Claude Code") + " command copied: paste it in a terminal.");
        });
    }

    void copy (const juce::String& text, const juce::String& done)
    {
        juce::SystemClipboard::copyTextToClipboard (text);
        note = done;
        refresh();
    }

    void chooseBridge()
    {
        chooser = std::make_unique<juce::FileChooser> ("Locate konisynth_mcp.py", juce::File(), "konisynth_mcp.py");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<AgentPanel> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                rememberFile().getParentDirectory().createDirectory();
                rememberFile().replaceWithText (fc.getResult().getFullPathName());
                safe->refresh();
            });
    }

    void timerCallback() override { refresh(); }

    void refresh()
    {
        const auto on = koni.isAgentEnabled();
        if (enable.getToggleState() != on)
            enable.setToggleState (on, juce::dontSendNotification);
        const auto clients = koni.agentClients();
        status.setText (on ? "On: listening on localhost:" + juce::String (koni.agentPort()) + ". "
                                 + (clients > 0 ? juce::String (clients) + (clients == 1 ? " agent connected." : " agents connected.")
                                                : juce::String ("No agent connected yet: ask yours to connect."))
                           : juce::String ("Off: no port open."),
                        juce::dontSendNotification);
        const auto script = targetScript();
        const auto venv = script.existsAsFile() && script.getParentDirectory().getChildFile (venvPython).existsAsFile();
        juce::String info;
        if (! script.existsAsFile())
            info << "Bridge: not found. Copy install command downloads it to " << script.getParentDirectory().getFullPathName()
                 << " (or copy there the KoniSynth-mcp folder of the release zip).";
        else
        {
            info << "Bridge: " << script.getFullPathName();
            if (downloadOutdated())
                info << " (" << downloadedTag() << "; this KoniSynth is " << releaseTag() << ": run the install command again to update)";
        }
        info << "\nPython environment: " << (venv ? "ready (.venv)." : pythonInPath() ? "not installed yet (Copy install command)."
                                                                                    : "Python not found in PATH: install Python 3.10+ first.");
        if (note.isNotEmpty())
            info << "   " << note;
        bridgeInfo.setText (info, juce::dontSendNotification);
    }

    KoniSynthProcessor& koni;
    juce::ToggleButton enable;
    juce::Label status, steps, bridgeInfo;
    juce::TextButton install, reg, locate;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note;
};
