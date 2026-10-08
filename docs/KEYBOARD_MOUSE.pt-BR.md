# Teclado e mouse

[English](KEYBOARD_MOUSE.md) · **Português**

O bbport se joga com teclado e mouse como um jogo de PC: o layout de teclas do Dark Souls III e
uma câmera no mouse que gira por ângulos exatos, dentro do próprio código de câmera do jogo.
Teclado, mouse e controle funcionam ao mesmo tempo.

## Teclas padrão

| Ação | Tecla |
|---|---|
| Andar | W A S D |
| Andar devagar (segurar) | Alt esquerdo |
| Câmera | Mouse (também I J K L no teclado) |
| Esquivar / correr (segurar) | Espaço |
| Pular | Espaço de novo enquanto corre (como no Dark Souls III), ou C |
| Travar mira / centralizar câmera | Q ou clique da roda |
| Ataque / ataque forte (R1 / R2) | Botão esquerdo / Shift + botão esquerdo |
| Transformar arma (L1) | Botão direito |
| Arma de fogo (L2) | Shift + botão direito ou Ctrl esquerdo |
| Interagir | E |
| Usar item rápido | R |
| Frasco de sangue | F |
| Trocar item rápido / direcional para cima | ↓ / ↑ ou roda para baixo / cima |
| Trocar arma da mão esquerda / direita | ← / → ou Shift + roda |
| Menu do jogo (Options) | Tab |
| Gestos (touchpad) | G |
| Confirmar / voltar nos menus | Enter / Esc |
| Menu de configurações do port | Insert |

O Bloodborne usa o mesmo botão (Círculo) para *voltar* e para *esquivar*, e o port não tem como
saber quando um menu está aberto. Por isso o Esc também faz backstep fora dos menus, e o menu do
jogo fica no Tab.

## Aba Controles

A aba **Controles** do launcher troca a tecla de qualquer ação: clique num campo e aperte uma
tecla ou botão do mouse; segure Shift, Ctrl ou Alt para uma combinação (como Shift + botão
esquerdo). Cada ação aceita até três entradas, e ⚠ marca uma entrada usada por mais de uma ação.
**Restaurar padrão** volta ao layout acima. As configurações são salvas em `keybinds.ini`, ao lado
do `bbport.ini`, quando você aperta JOGAR; o jogo as lê ao iniciar. O arquivo também pode ser
editado à mão; apagá-lo restaura o padrão.

| Configuração | `keybinds.ini` | Padrão |
|---|---|---|
| O mouse gira a câmera | `mouse_camera` | ligado |
| Sensibilidade do mouse | `mouse_sensitivity` | 1,0 |
| Sensibilidade vertical (× horizontal) | `mouse_sensitivity_y` | 1,0 |
| Inverter horizontal / vertical | `mouse_invert_x` / `mouse_invert_y` | desligado |
| Sem auto-rotação da câmera ao se mover | `mouse_no_auto_rotation` | ligado |
| Velocidade de andar devagar | `walk_tilt` | 0,4 |
| Pular com a tecla de esquiva enquanto corre | `ds3_jump` | ligado |

A **sensibilidade** usa a escala dos jogos Source, como Deadlock e Counter-Strike: cada contagem
do mouse gira a câmera 0,022° × sensibilidade. Copie o valor de `sensitivity` desses jogos e a
câmera gira o mesmo tanto por centímetro de mouse.

## A câmera do mouse

Um mouse convertido em analógico direito nunca parece um jogo de PC: o Bloodborne acelera a
câmera enquanto o analógico fica inclinado, nivela a altura enquanto ela gira, gira a câmera atrás
do personagem quando ele anda e desenha a câmera a partir de uma posição que segue a ideal com uma
mola. Por isso o port gira a câmera dentro do próprio código de câmera do jogo
(`src/runtime_camhook.c`):

- A atualização do objeto da câmera (imagem `0x143a000..0x1440200` da versão 1.09) foi encontrada
  com watchpoints de escrita do processador nos ângulos da câmera e lida com um desmontador. Todos
  os caminhos dela para a câmera livre se encontram em `image+0x143ce67`, depois que os ângulos do
  frame são escritos (vertical `+0x140`, horizontal `+0x144`, radianos) e antes de a câmera ser
  montada a partir deles.
- Um desvio ali executa um pequeno trecho que chama o port, na thread da câmera do jogo, quando o
  mouse se moveu. Ele soma o giro do mouse ao horizontal e aos verticais atual e alvo (`+0x150`),
  dentro dos limites de altura do próprio jogo, e gira a posição desenhada (`+0x100`) em volta do
  pivô (`+0xd0`) pelo mesmo ângulo, para que a mola da câmera não tenha nada a recuperar.
- As escritas do horizontal que giram a câmera atrás do personagem em movimento viram NOPs (o
  patch da comunidade *Disable Camera Auto Rotation via Movement*, de Imedved e Kyo), a não ser com
  `mouse_no_auto_rotation = 0`.
- O movimento do mouse vai da thread da janela direto para o trecho, sem esperar a próxima leitura
  de controle do jogo.

Medido no jogo: um giro de 0,30 rad aparece completo, com quatro casas decimais, no frame
seguinte (antes, como analógico: 38% depois de um frame e 92% depois de 100 ms). O analógico, o
lock-on, as colisões e o resto da câmera continuam sendo do jogo.

O enxerto confere o código do jogo antes de mudar qualquer coisa. Em outra versão do jogo, ou onde
ele ainda não existe (Linux), o mouse volta a funcionar como o analógico direito, e um aviso no
jogo informa isso.

## Arquivos

| Arquivo | O quê |
|---|---|
| `src/runtime_pad.c` | Teclas de teclado e mouse, `keybinds.ini`, o modo analógico de reserva |
| `src/runtime_camhook.c` | O enxerto na câmera (Windows) |
| `gpu/shim/window.cpp`, `gpu/shim/bbgpu.cpp` | Modo de mouse relativo, movimento do mouse para o enxerto |
| `gpu/shim/bbport_overlay.cpp` | Avisos na tela |
| `launcher/bbport_controls.py` | A aba Controles do launcher |
| `launcher/bbport_lang.py` | Os textos dela nos idiomas do launcher |
| `tests/test_pad.c` | Teste de controle, teclado e mouse |
