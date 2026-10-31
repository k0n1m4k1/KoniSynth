"""Genera las fórmulas de Math Models como SVG (Resources/formulas/*.svg) para el diálogo Formula... (UI/MathModelPanel.h).

Usa el motor de fórmulas de matplotlib (mathtext, subconjunto de LaTeX) con los glifos convertidos en trazados: el SVG no
depende de fuentes instaladas. Color negro: la UI lo sustituye por el del skin. Solo hace falta para regenerarlas:
    python tools/make_formula_svgs.py        (matplotlib >= 3.8)
"""
import os
import matplotlib
matplotlib.use('svg')
import matplotlib.pyplot as plt

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Resources', 'formulas')
plt.rcParams['svg.fonttype'] = 'path'
plt.rcParams['svg.hashsalt'] = 'konisynth'  # identificadores estables: el SVG no cambia si se regenera
plt.rcParams['mathtext.fontset'] = 'cm'

# Una lista de líneas por modelo (en el orden del parámetro stringExcite). Una idea por línea: el diálogo las muestra
# sobre fondo claro en una columna de ~520 px, y una línea larga se encogería hasta ser ilegible. Las líneas que KoniSynth
# cambia respecto al modelo publicado empiezan por '*' (no se dibuja): la fórmula lleva debajo un pie que lo dice, y la
# explicación del diálogo detalla el cambio
FORMULAS = {
    'noise-burst': [r'$y[n]=\rho\,\left[(1-S)\,y[n-N]+S\,y[n-N-1]\right]$',
                    r'$y[0..N-1]=\mathrm{LP}_{Bright}(\mathrm{noise})\;-\;\mathrm{comb}_{\beta}$'],
    'pick':        [r'$y_0(x)=x/\beta\qquad (x<\beta)$',
                    r'$y_0(x)=(1-x)/(1-\beta)\qquad (x\geq\beta)$',
                    r'$a_k\propto\sin(\pi k\beta)\,/\,k^2$'],
    'bow':         [r'$\Delta v=v_{bow}-(v_{bridge}+v_{nut})$',
                    r'$v_{new}=\Delta v\;T(\Delta v),\qquad s=5-4\,P$',
                    r'$T(x)=\min\!\left(0.98,\;\max\!\left(0.01,\;\left(|s\,x|+0.75\right)^{-4}\right)\right)$'],
    'hammer':      [r'$F=K\,x^{p},\qquad p=2.5$',
                    r'*$K=10^{\,6+4\,Felt}$',
                    r'$m\,\dot{v}_h=-F,\qquad v_{new}=F/2$'],
    'ebow':        [r'*$v_{new}=0.005\,G\,\tanh(10\,v_s)$',
                    r'*$G=0.5+1.5\,Drive$'],
    'modal':       [r'$s(t)=w(t)\,\sum_{k=1}^{K}A_k\,e^{-t/(\tau_k D)}\,\sin(2\pi\,r_k\,f\,t)$',
                    r'*$w(t)=\min(1,\;t/1.5\,\mathrm{ms}),\qquad D=Decay/3\,\mathrm{s}$'],
    'buzz':        [r'$y_x^{n+1}=y_{x+1}^{n}+y_{x-1}^{n}-y_x^{n-1}$',
                    r'*$y_x\geq p(x)=-d\,(x/J)^2$'],
    'reed':        [r'$x[n]=b_1\,p_e[n-1]+a_1\,x[n-1]+a_2\,x[n-2]$',
                    r'$V=a\,(p_e[n-1]-u_e[n-1])-b\,(p_e[n-D]+u_e[n-D])$',
                    r'$u_e=\frac{\mathrm{sgn}(\gamma-V)}{2}\left(-W^2+W\sqrt{W^2+4|\gamma-V|}\right),\quad W=\zeta\,(1-\gamma+x)_{+}$',
                    r'$p_e=u_e+V$',
                    r'*$p_{ext}=\mathrm{LP}_{1.5k}\left(\Delta(p_e+u_e)\right)$'],
    'flute':       [r'$t=-\mathrm{LP}(y[n-1]),\qquad x_{jet}=p_b-0.5\,t$',
                    r'$j=\mathrm{clip}(x\,(x^2-1),\,\pm 1)$',
                    r'*$x_{bore}=\mathrm{DC}(j)-0.5\,t$',
                    r'*$y_{out}=y+s\,\mathrm{HP}_{1k}(y),\qquad s=-6.3+17.1\,v$'],
    'brass':       [r'$\Delta p=0.3\,p_b-0.85\,y[n-1]$',
                    r'*$f_{lip}=0.965\,f\,4^{\,2\,Lip-1},\qquad Q_{lip}=15$',
                    r'$a=\min\!\left(1,\;\mathrm{lip}(\Delta p)^2\right)$',
                    r'$x_{bore}=\mathrm{DC}\left(a\,0.3\,p_b+(1-a)\,0.85\,y[n-1]\right)$',
                    r'*$y_{out}=\mathrm{HP}^2_{1.5k}(y)$'],
    'banded':      [r'$y_k[n]=\mathrm{BP}_k\left(x[n]+g_k\,y_k[n-1-L_k]\right),\qquad y=4\sum_k y_k$',
                    r'*$L_k=\frac{f_s}{r_k\,f}-1-\tau_k,\qquad |\mathrm{BP}_k(r_k f)|=1$',
                    r'*$e_k\leftarrow e_k\left(\frac{1+(r_k f/f_c)^2}{1+(r_1 f/f_c)^2}\right)^{-4},\quad f_c=2012\cdot1.2306^{(v-80)/40}\,\mathrm{Hz}$',
                    r'$x=\Delta v\;T(\Delta v)/K,\qquad \Delta v=v_{bow}-0.999\sum_k y_k[n-1-L_k]$',
                    r'$T(x)=\min\!\left(0.98,\;\max\!\left(0.01,\;\left(|s\,x|+0.75\right)^{-4}\right)\right),\quad s=10-9\,P$'],
    'shaker':      [r'$E[n]=d_s\,E[n-1],\qquad P(\mathrm{hit})=N/1024$',
                    r'$\mathrm{hit}:\;S\leftarrow S+E,\qquad S[n]=d_l\,S[n-1]$',
                    r'$y=\mathrm{EQ}\left(\sum_i \mathrm{Res}_{f_i,\,r_i}\!\left(G\,a_i\,S_{\mathrm{hit}}\right)\right),\qquad G=\ln N/N$',
                    r'*$f_i=f_{i,0}\;\frac{f}{261.6}\;4^{\,Tone-0.5}$'],
    'stiff':       [r'$\mathrm{Stiff}:\;H(z)=\left(\frac{a+z^{-1}}{1+a\,z^{-1}}\right)^{16},\qquad f_k=k\,f_0\sqrt{1+B\,k^2}$',
                    r'*$a:\;f_{12}=12\,f_0\sqrt{1+144\,B},\qquad B=10^{-5+2\,Stiff}$',
                    r'$\mathrm{Tension}:\;P=\sum_k\left(s_k+s_{N-1-k}\right)^2,\qquad N\rightarrow N/(1+G\,I(P))$'],
    'mallet':      [r'$f=k\,x^{\alpha}\,(1+\mu\,\dot{x}),\qquad \alpha=2.8,\qquad x=y_h-\sum_k p_k>0$',
                    r'$m\,\ddot{y}_h=-f,\qquad \ddot{p}_k+\frac{2}{\tau_k}\,\dot{p}_k+\omega_k^2\,p_k=f$',
                    r'*$k=\frac{(\alpha+1)\,m}{2\,x_m^{\alpha+1}},\qquad x_m=\frac{t_c}{2I},\qquad t_c=10\,\mathrm{ms}\cdot 0.03^{\,H}$',
                    r'*$y=\sum_k A_k\,\omega_k\,p_k\,/\,2m$'],
}

os.makedirs(OUT, exist_ok=True)
for name, lines in FORMULAS.items():
    fig = plt.figure(figsize=(0.01, 0.01))
    for i, line in enumerate(lines):
        fig.text(0, -0.5 * i, line.lstrip('*'), fontsize=16, color='#000000', transform=fig.dpi_scale_trans)  # 0,5 pulgadas por línea
    if any(line.startswith('*') for line in lines):
        fig.text(0, -0.5 * len(lines) + 0.12, 'Adapted by KoniSynth from the published model', fontsize=12, fontstyle='italic',
                 color='#000000', transform=fig.dpi_scale_trans)
    path = os.path.join(OUT, name + '.svg')
    fig.savefig(path, format='svg', transparent=True, bbox_inches='tight', pad_inches=0.02, metadata={'Date': None})
    plt.close(fig)
    print(path, os.path.getsize(path), 'B')

# Versiones para la documentación (docs/images/math-models/*.svg, enlazadas desde docs/engines/synth.md): las mismas fórmulas
# sobre fondo marfil con margen, legibles también en un visor de Markdown con tema oscuro. Las de arriba (Resources/formulas)
# no cambian: la app las incrusta
DOC_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'images', 'math-models')
DOC_BG = '#fffdf5'

# Comparaciones de los modelos adaptados: forma publicada (tal como está en la fuente citada en el manual) frente a la de
# KoniSynth, solo las líneas que cambian; la primera línea es el rótulo. Fuentes: STK (Bowed, Flute, Brass, BandedWG, Shakers;
# github.com/thestk/stk) y Guillemain, Kergomard y Voinier, JASA 118 (2005), ec. (13) y (14), sección IV.F y fig. 14
COMPARE = {
    'bow': (['Published: STK Bowed',
             r'$H_{bridge}(z)=0.95\,\frac{1-p}{1-p\,z^{-1}},\qquad p=0.75-0.2\cdot 22050/f_s$',
             r'$N=f_s/f-4,\qquad y_{out}=0.1248\,\mathrm{Body}_{6}(y_{bridge})$'],
            ['KoniSynth',
             r'$H_{bridge}(z)=k\,\frac{1-p}{1-p\,z^{-1}},\qquad p=0.95\,(1-Bright)$',
             r'$|H_{bridge}(e^{j\omega_0})|=g=10^{-3/(f\,Decay)}$',
             r'$N=f_s/f-\tau_H(\omega_0),\qquad y_{out}=y_{bridge}$']),
    'modal': (['Published: modal synthesis',
               r'$s(t)=\sum_{k=1}^{K}A_k\,e^{-t/\tau_k}\,\sin(2\pi\,f_k\,t)$'],
              None),  # la forma de KoniSynth es modal.svg entera
    'reed': (['Published: Guillemain, Kergomard & Voinier (2005)',
              r'$p_{ext}[n]=(p_e+u_e)[n]-(p_e+u_e)[n-1]$',
              r'$D=2f_eL/c=f_e/(2f)\;\;\mathrm{(whole\ samples)},\qquad q_r=0.3$'],
             ['KoniSynth',
              r'$p_{ext}=\mathrm{LP}_{1.5k}\left(\Delta(p_e+u_e)\right)$',
              r'$D=f_s/(2f)-\tau_{loss}-1.8\cdot 10^{-5}f_s\;\;\mathrm{(fractional)},\qquad q_r=0.6$']),
    'flute': (['Published: STK Flute',
               r'$x_{bore}=\mathrm{DC}(j)+0.5\,t$',
               r'$L_{bore}=\frac{f_s}{2f/3}-\tau_{LP}-1,\qquad L_{jet}=0.32\,L_{bore}$',
               r'$p_b=(1.1+0.2\,v)/0.8,\qquad y_{out}=0.3\,y$'],
              ['KoniSynth',
               r'$x_{bore}=\mathrm{DC}(j)-0.5\,t$',
               r'$L_{bore}=\frac{f_s}{f}-\tau_{LP}-1$',
               r'$L_{jet}=\left(\frac{2f_s}{f}-1-L_{bore}-\tau_{LP}-\tau_{DC}\right)\,\frac{Jet}{0.32}$',
               r'$p_b=(1+0.3\,v)/0.8,\qquad y_{out}=y+s\,\mathrm{HP}_{1k}(y),\quad s=-6.3+17.1\,v$']),
    'brass': (['Published: STK Brass',
               r'$f_{lip}=f\,4^{\,2\,Lip-1},\qquad r=0.997,\qquad g=0.03$',
               r'$L=2f_s/f+3,\qquad y_{out}=y$'],
              ['KoniSynth',
               r'$f_{lip}=0.965\,f\,4^{\,2\,Lip-1},\qquad r=e^{-\pi f_{lip}/(Q\,f_s)},\quad Q=15$',
               r'$g=0.7\,(1-2r\cos\omega_{lip}+r^2)\,/\,0.24$',
               r'$L=2f_s/f-1-\tau_{DC},\qquad y_{out}=\mathrm{HP}^2_{1.5k}(y)$']),
    'banded': (['Published: STK BandedWG',
                r'$L_k=\left\lfloor\frac{f_s}{r_k\,f}\right\rfloor,\qquad f\leq 1568\,\mathrm{Hz}$',
                r'$g_k=g_{k,0}$',
                r'$\mathrm{strike}:\;\lfloor L_k/L_{min}\rfloor\ \mathrm{samples\ of}\ e_k\,a/K\ \mathrm{into\ each\ delay}$'],
               ['KoniSynth',
                r'$L_k=\frac{f_s}{r_k\,f}-1-\tau_k,\qquad r_k\,f<0.45\,f_s$',
                r'$g_k=\min(0.9995,\,g_{k,0})^{3\,\mathrm{s}/Decay}\,/\,|\mathrm{BP}_k(r_k f)|$',
                r'$\mathrm{bars}:\;e_k\leftarrow e_k\left(\frac{1+(r_k f/f_c)^2}{1+(r_1 f/f_c)^2}\right)^{-4},\quad f_c=2012\cdot1.2306^{(v-80)/40}\,\mathrm{Hz}$']),
    'shaker': (['Published: STK Shakers (constants per sample at 44.1 kHz)',
                r'$f_i=f_{i,0}\;4^{\,M-0.5}\qquad (M=\mathrm{mod\ wheel})$',
                r'$y=0\quad\mathrm{when}\quad E<0.001$'],
               ['KoniSynth',
                r'$f_i=f_{i,0}\;\frac{f}{261.6}\;4^{\,Tone-0.5},\qquad d\rightarrow d^{\,44100/f_s}$',
                r'$E<0.001:\;\mathrm{no\ new\ hits,\ the\ resonances\ ring\ out}$']),
    'mallet': (['Published: Avanzini & Rocchesso (DAFx 2001)',
                r'$f(x,\dot{x})=-k\,x^{\alpha}-\lambda\,x^{\alpha}\,\dot{x}\qquad (k,\ \lambda,\ m_h\ \mathrm{given})$',
                r'$f[n]\ \mathrm{solved\ by\ Newton-Raphson\ in\ the\ same\ sample}$'],
               ['KoniSynth',
                r'$k\ \mathrm{from\ the\ contact\ time}\ t_c\ \mathrm{(Hertz\ impact)},\qquad \mu=\lambda/k$',
                r'$f[n]\ \mathrm{from}\ x[n-1],\ \dot{x}[n-1]\ \mathrm{(explicit,\ as\ in\ SDT)}$']),
}


def doc_svg(path, lines, footer=None, caption=None):
    # Texto en coordenadas de la figura (fracción de una figura de 20 x 10 pulgadas), no en dpi_scale_trans como arriba:
    # bbox_inches='tight' recoloca transFigure pero no dpi_scale_trans, y entonces el viewBox del SVG deja fuera las líneas 2
    # y siguientes en un navegador (JUCE dibuja el contenido entero y no lo nota)
    width, height = 20.0, 10.0
    fig = plt.figure(figsize=(width, height))
    y = height - 0.5  # pulgadas desde abajo

    def put(text, size, **style):
        fig.text(0.2 / width, y / height, text, fontsize=size, **style)

    if caption:  # rótulo encima (Published / KoniSynth)
        put(caption, 12, fontstyle='italic', color='#555555')
        y -= 0.45
    for line in lines:
        put(line.lstrip('*'), 16, color='#000000')
        y -= 0.5
    if footer:
        y += 0.12
        put(footer, 12, fontstyle='italic', color='#555555')
    fig.savefig(path, format='svg', facecolor=DOC_BG, bbox_inches='tight', pad_inches=0.18, metadata={'Date': None})
    plt.close(fig)
    print(path, os.path.getsize(path), 'B')


os.makedirs(DOC_OUT, exist_ok=True)
for name, lines in FORMULAS.items():
    adapted = any(line.startswith('*') for line in lines)
    doc_svg(os.path.join(DOC_OUT, name + '.svg'), lines, 'Adapted by KoniSynth from the published model' if adapted else None)
for name, (published, konisynth) in COMPARE.items():
    doc_svg(os.path.join(DOC_OUT, name + '-published.svg'), published[1:], caption=published[0])
    if konisynth:
        doc_svg(os.path.join(DOC_OUT, name + '-konisynth.svg'), konisynth[1:], caption=konisynth[0])
