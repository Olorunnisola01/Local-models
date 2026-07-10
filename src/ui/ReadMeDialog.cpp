#include "ReadMeDialog.h"

#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QString readMeHtml() {
    return QStringLiteral(R"html(
<h2>Text Tags &amp; Examples</h2>
<p>Type tags directly in the <b>Text to synthesize</b> box (Single Speaker) or dialogue lines (Multi-Speaker).
Tags are case-insensitive unless noted. See the <b>Pronunciation</b> tab for custom word replacements.</p>

<h3>Universal — all providers</h3>
<table border="1" cellpadding="6" cellspacing="0" width="100%">
<tr><th>Tag</th><th>What it does</th><th>Example</th></tr>
<tr>
  <td><code>[pause=400ms]</code><br><code>[pause 400ms]</code></td>
  <td>Inserts silence <i>after</i> the text that precedes the tag. Works with every provider.</td>
  <td><code>Hello there.[pause=400ms] How are you?</code></td>
</tr>
</table>
<p><b>Tips:</b> Use 150–300&nbsp;ms between phrases, 400–800&nbsp;ms before a new thought, and 800–1500&nbsp;ms for dramatic beats.
A pause at the very start of the text creates leading silence.</p>
<pre>Welcome back.[pause=300ms]
Today we cover three topics.[pause=600ms]

First — pronunciation.[pause=400ms] Second — pacing.[pause=800ms]
And finally, emotion.</pre>

<h3>Automatic pacing (no tags needed)</h3>
<p>Open <b>Settings…</b> to tune gaps applied when long text is split into chunks:</p>
<ul>
  <li><b>Sentence gap</b> — pause after <code>.</code> <code>!</code> <code>?</code> (default 150&nbsp;ms)</li>
  <li><b>Paragraph gap</b> — pause after blank lines (default 600&nbsp;ms)</li>
  <li><b>Max characters per chunk</b> — how text is split for synthesis</li>
</ul>
<p>Inline <code>[pause=…]</code> tags add on top of these automatic gaps.</p>

<h3>Microsoft Edge (online)</h3>
<table border="1" cellpadding="6" cellspacing="0" width="100%">
<tr><th>Tag</th><th>What it does</th><th>Example</th></tr>
<tr>
  <td><code>[emph]word[/emph]</code><br><code>*word*</code></td>
  <td>Moderate emphasis via SSML. Enable <b>Natural Humanizer</b> for more natural prosody.</td>
  <td><code>This is [emph]very[/emph] important.</code></td>
</tr>
</table>
<pre>[emph]Attention[/emph] all passengers.[pause=500ms]
The *next* train arrives in two minutes.</pre>
<p>Pause tags still work with Edge. Emphasis tags are stripped for offline engines (Supertonic, Kokoro, Piper).</p>

<h3>Fish Audio S2 (Kaggle)</h3>
<p>When <b>Fish Audio S2</b> is the provider, square-bracket cues control emotion, tone, and sound effects.
Place sentence-level emotions at the start of a sentence. Combine up to ~3 cues for layered delivery.</p>

<h4>Basic emotions</h4>
<p><code>[happy]</code> <code>[sad]</code> <code>[angry]</code> <code>[excited]</code> <code>[calm]</code>
<code>[nervous]</code> <code>[confident]</code> <code>[surprised]</code> <code>[satisfied]</code>
<code>[delighted]</code> <code>[scared]</code> <code>[worried]</code> <code>[upset]</code>
<code>[frustrated]</code> <code>[depressed]</code> <code>[empathetic]</code> <code>[embarrassed]</code>
<code>[disgusted]</code> <code>[moved]</code> <code>[proud]</code> <code>[relaxed]</code>
<code>[grateful]</code> <code>[curious]</code> <code>[sarcastic]</code></p>

<h4>Advanced emotions</h4>
<p><code>[disdainful]</code> <code>[unhappy]</code> <code>[anxious]</code> <code>[hysterical]</code>
<code>[indifferent]</code> <code>[uncertain]</code> <code>[doubtful]</code> <code>[confused]</code>
<code>[disappointed]</code> <code>[regretful]</code> <code>[guilty]</code> <code>[ashamed]</code>
<code>[jealous]</code> <code>[envious]</code> <code>[hopeful]</code> <code>[optimistic]</code>
<code>[pessimistic]</code> <code>[nostalgic]</code> <code>[lonely]</code> <code>[bored]</code>
<code>[contemptuous]</code> <code>[sympathetic]</code> <code>[compassionate]</code>
<code>[determined]</code> <code>[resigned]</code></p>

<h4>Tone &amp; volume</h4>
<p><code>[in a hurry tone]</code> <code>[shouting]</code> <code>[screaming]</code>
<code>[whispering]</code> <code>[soft tone]</code></p>

<h4>Sound effects</h4>
<p><code>[laughing]</code> <code>[chuckling]</code> <code>[sobbing]</code> <code>[crying loudly]</code>
<code>[sighing]</code> <code>[groaning]</code> <code>[panting]</code> <code>[gasping]</code>
<code>[yawning]</code> <code>[snoring]</code></p>
<p>Shorthand tags like <code>[laugh]</code> <code>[whisper]</code> <code>[cry]</code> are also commonly used.</p>

<h4>Pauses &amp; atmosphere</h4>
<p><code>[break]</code> short pause &nbsp;|&nbsp; <code>[long-break]</code> extended pause<br>
<code>[audience laughing]</code> <code>[background laughter]</code> <code>[crowd laughing]</code></p>

<h4>Examples</h4>
<pre>[happy] What a beautiful morning![pause=300ms]
[sad][whispering] I will miss you so much.
[excited][laughing] We did it! Ha ha!
[angry][shouting] Stop right there!
[calm] Take a deep breath.[break] Now begin.</pre>
<pre>[happy] I got the promotion!
[uncertain] But it means relocating.[pause=500ms]
[sad] I'll miss everyone here.
[determined] I'm going to make it work!</pre>
<p><b>Note:</b> Fish emotion tags are only interpreted by the Fish Audio S2 engine. Other providers ignore or strip them.</p>

<h3>Offline engines (Supertonic, Kokoro, Piper)</h3>
<ul>
  <li><code>[pause=…]</code> — fully supported</li>
  <li><code>[emph]…[/emph]</code> / <code>*…*</code> — stripped (spoken as plain text)</li>
  <li>Fish Audio emotion tags — not supported; use Fish Audio S2 provider instead</li>
  <li>Use the <b>Pronunciation</b> tab to fix names, acronyms, and tricky words before synthesis</li>
</ul>

<h3>Quality checklist</h3>
<ol>
  <li>Break long passages with <code>[pause=…]</code> at commas and clause boundaries</li>
  <li>Put a blank line between paragraphs for automatic paragraph gaps</li>
  <li>Match tags to your provider (Edge emphasis vs. Fish emotions)</li>
  <li>Preview processed text on the <b>Pronunciation</b> tab before rendering</li>
  <li>Fine-tune sentence/paragraph gaps in <b>Settings…</b> for your speaking rate</li>
</ol>
)html");
}

} // namespace

ReadMeDialog::ReadMeDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Read me — Text Tags & Examples");
    resize(720, 560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* layout = new QVBoxLayout(this);

    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setHtml(readMeHtml());
    layout->addWidget(browser, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}