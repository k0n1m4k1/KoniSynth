// Diálogo "Agent (MCP)": activar el control por agente, ver su estado y preparar la conexión con el agente (una vez).
// Cómo funciona: KoniSynth abre un servidor local (Agent/AgentServer.h, solo 127.0.0.1, primer puerto libre de 47800-47899) y
// anuncia puerto y token en %APPDATA%/KoniSynth/agent. El puente MCP (Agent/KoniSynth-mcp/konisynth_mcp.py, Python) lo arranca
// el propio agente cuando está registrado en él; el usuario no lanza nada a mano. Se incluye desde KoniSynth.cpp (mismo TU).
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
        steps.setText ("Once: 1) install the bridge's Python packages (Copy install command, run it in a terminal). "
                       "2) Register it in your agent (Copy Claude Code command, run it; other MCP clients: the same python + script). "
                       "Then, every time: turn Agent control on here and ask your agent to connect. The agent starts the bridge itself.",
                       juce::dontSendNotification);
        steps.setColour (juce::Label::textColourId, Skin::white().withAlpha (0.8f));
        install.setButtonText ("Copy install command");
        install.setTooltip ("Creates the bridge's Python environment (.venv) and installs its pinned packages (mcp, numpy).");
        install.onClick = [this] { copy (installCommand(), "Install command copied: paste it in a terminal."); };
        reg.setButtonText ("Copy Claude Code command");
        reg.setTooltip ("Registers KoniSynth in Claude Code for every project (user scope).");
        reg.onClick = [this] { copy (registerCommand(), "Claude Code command copied: paste it in a terminal."); };
        locate.setButtonText ("Locate bridge...");
        locate.setTooltip ("Choose konisynth_mcp.py if it is not found next to KoniSynth or in its source folder.");
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

    // Ruta del puente: junto al ejecutable o al plugin (zip), la elegida con Locate (recordada) o la del repositorio
    static juce::File bridgeScript()
    {
        const auto saved = juce::File (rememberFile().loadFileAsString().trim());
        if (saved.existsAsFile())
            return saved;
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (auto dir = self.getParentDirectory(); dir.exists() && ! dir.isRoot(); dir = dir.getParentDirectory())
        {
            const auto f = dir.getChildFile ("KoniSynth-mcp").getChildFile ("konisynth_mcp.py");
            if (f.existsAsFile())
                return f;
        }
       #ifdef KONI_REPO_DIR
        const auto repo = juce::File (KONI_REPO_DIR).getChildFile ("Agent/KoniSynth-mcp/konisynth_mcp.py");
        if (repo.existsAsFile())
            return repo;
       #endif
        return {};
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

    // Python del entorno del puente (.venv) si existe; si no, el del PATH
    static juce::String pythonFor (const juce::File& script)
    {
        const auto venv = script.getParentDirectory().getChildFile (venvPython);
        return venv.existsAsFile() ? venv.getFullPathName() : juce::String (systemPython);
    }

    static bool pythonInPath()
    {
        for (const auto& dir : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), pathSep, "\""))
            if (dir.isNotEmpty() && juce::File (dir.trim()).getChildFile (pythonExe).existsAsFile())
                return true;
        return false;
    }

    static juce::String quote (const juce::String& s) { return "\"" + s + "\""; }

    juce::String installCommand() const
    {
        const auto dir = bridgeScript().getParentDirectory();
        return "cd " + quote (dir.getFullPathName()) + " && " + systemPython + " -m venv .venv && "
               + quote (dir.getChildFile (venvPython).getFullPathName()) + " -m pip install -r requirements.txt";
    }

    juce::String registerCommand() const
    {
        const auto script = bridgeScript();
        auto python = pythonFor (script);
        if (python == systemPython) // aún sin .venv: el registro apunta al que creará el comando de instalación
            python = script.getParentDirectory().getChildFile (venvPython).getFullPathName();
        return "claude mcp add KoniSynth --scope user -- " + quote (python) + " " + quote (script.getFullPathName());
    }

    void copy (const juce::String& text, const juce::String& done)
    {
        if (! bridgeScript().existsAsFile())
        {
            bridgeInfo.setText ("Bridge not found: use Locate bridge... first.", juce::dontSendNotification);
            return;
        }
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
        const auto script = bridgeScript();
        const auto venv = script.existsAsFile() && script.getParentDirectory().getChildFile (venvPython).existsAsFile();
        juce::String info = script.existsAsFile() ? "Bridge: " + script.getFullPathName() : juce::String ("Bridge: not found (Locate bridge...).");
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
