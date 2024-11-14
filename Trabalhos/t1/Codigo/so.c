// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"

#include <stdlib.h>
#include <stdbool.h>

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define TAM_TABELA_PROCESSOS 8

typedef struct processo_t processo_t;

typedef enum {
  MORTO,
  BLOQUEADO,
  PRONTO
} process_estado_t;


struct processo_t {
  int process_id;
  int reg_a;
  int reg_x;
  int reg_pc;
  process_estado_t estado;

  int terminal;
};

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  es_t *es;
  console_t *console;
  bool erro_interno;

  // t1: tabela de processos, processo corrente, pendências, etc

  processo_t *processo_corrente;
  processo_t **tabela_processos;
  int qnt_processos;
  int max_processos;
};


// função de calculo de terminal
int terminal_processo(int terminal, int tipo){
  return terminal + tipo;
}


// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares

// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);


// TABELA DE PROCESSOS {{{1
// funções auxiliares para a tabela de processos
static processo_t *cria_processo(int process_id, int reg_pc);
static void mata_processo(so_t *self, int process_id);
static processo_t *busca_processo(so_t *self, int process_id);
static processo_t *so_adiciona_processo(so_t *self, char *nome_do_executavel);

static processo_t *cria_processo(int process_id, int reg_pc){
  processo_t *processo = malloc(sizeof(processo_t));

  processo->process_id = process_id;

  processo->reg_a = 0;
  processo->reg_x = 0;
  processo->reg_pc = reg_pc;

  processo->estado = PRONTO;

  processo->terminal = (process_id % 4) * 4;

  return processo;
}

static void mata_processo(so_t *self, int process_id){
  if (process_id <= 0 || process_id > self->qnt_processos) return;

  processo_t *proc = self->tabela_processos[process_id-1];
  proc->estado = MORTO;
}

static processo_t *busca_processo(so_t *self, int process_id){
  if (process_id <= 0 || process_id > self->qnt_processos) return NULL;

  return self->tabela_processos[process_id-1];
}

static processo_t *so_adiciona_processo(so_t *self, char *nome_do_executavel){
  int ender = so_carrega_programa(self, nome_do_executavel);

  if (ender < 0) {
    return NULL;
  }

  if (self->qnt_processos == self->max_processos){
    self->max_processos = self->max_processos * 2;
    self->tabela_processos = realloc(self->tabela_processos, self->max_processos * sizeof(*self->tabela_processos));
  }

  self->qnt_processos++;
  processo_t *processo = cria_processo(self->qnt_processos, ender);
  self->tabela_processos[self->qnt_processos - 1] = processo;
  self->processo_corrente = processo;

  return processo;
}

// CRIAÇÃO {{{1

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->es = es;
  self->console = console;
  self->erro_interno = false;

  self->qnt_processos = 0;
  self->max_processos = TAM_TABELA_PROCESSOS;
  self->processo_corrente = NULL;
  self->tabela_processos = malloc(self->max_processos * sizeof(processo_t *));

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor, 
  //   salva seu estado à partir do endereço 0, e desvia para o endereço
  //   IRQ_END_TRATADOR
  // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a 
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido acima)
  int ender = so_carrega_programa(self, "trata_int.maq");
  if (ender != IRQ_END_TRATADOR) {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }

  console_printf("1");

  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  free(self);
}


// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  //console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);
  // faz o atendimento da interrupção
  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido
  return so_despacha(self);
}

static void so_salva_estado_da_cpu(so_t *self)
{
  // t1: salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente. os valores dos registradores foram colocados pela
  //   CPU na memória, nos endereços IRQ_END_*
  // se não houver processo corrente, não faz nada
  processo_t *proc = self->processo_corrente;

  if (proc == NULL) return;

  int pc, a, x;

  mem_le(self->mem, IRQ_END_PC, &pc);
  mem_le(self->mem, IRQ_END_A, &a);
  mem_le(self->mem, IRQ_END_X, &x);

  proc->reg_pc = pc;
  proc->reg_a = a;
  proc->reg_x = x;
}

static void so_trata_pendencias(so_t *self)
{
  // t1: realiza ações que não são diretamente ligadas com a interrupção que
  //   está sendo atendida:
  // - E/S pendente
  // - desbloqueio de processos
  // - contabilidades

  for (int i = 0; i < self->qnt_processos; i++){
    processo_t *proc = self->tabela_processos[i];

    if (proc->estado == BLOQUEADO){
      int estado;
      int terminal = proc->terminal;

      if (es_le(self->es, terminal_processo(terminal, TECLADO_OK), &estado) == ERR_OK && estado != 0){
        proc->estado = PRONTO;
      }
    }
  }

}

// escalonador simples
static void so_escalona(so_t *self)
{
  // escolhe o próximo processo a executar, que passa a ser o processo
  //   corrente; pode continuar sendo o mesmo de antes ou não
  // t1: na primeira versão, escolhe um processo caso o processo corrente não possa continuar
  //   executando. depois, implementar escalonador melhor
  
  if (self->processo_corrente != NULL && self->processo_corrente->estado == PRONTO) {
    //console_printf("SO: processo corrente pronto");
    return;
  }

  for (int i = 0; i < self->qnt_processos; i++){
    if (self->tabela_processos[i]->estado == PRONTO){
      self->processo_corrente = self->tabela_processos[i];
      return;
    }
  }

  //console_printf("SO: não há processos prontos");
  self->processo_corrente = NULL;
  self->erro_interno = true;
}

static int so_despacha(so_t *self)
{
  // t1: se houver processo corrente, coloca o estado desse processo onde ele
  //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
  // o valor retornado será o valor de retorno de CHAMAC
  if (self->erro_interno) return 1;

  processo_t *proc = self->processo_corrente;

  if (proc == NULL) return 1;

  int pc = proc->reg_pc;
  int a = proc->reg_a;
  int x = proc->reg_x;

  mem_escreve(self->mem, IRQ_END_PC, pc);
  mem_escreve(self->mem, IRQ_END_A, a);
  mem_escreve(self->mem, IRQ_END_X, x);

  return 0;
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq) {
    case IRQ_RESET:
      so_trata_irq_reset(self);
      break;
    case IRQ_SISTEMA:
      so_trata_irq_chamada_sistema(self);
      break;
    case IRQ_ERR_CPU:
      so_trata_irq_err_cpu(self);
      break;
    case IRQ_RELOGIO:
      so_trata_irq_relogio(self);
      break;
    default:
      so_trata_irq_desconhecida(self, irq);
  }
}

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t *self)
{
  // t1: deveria criar um processo para o init, e inicializar o estado do
  //   processador para esse processo com os registradores zerados, exceto
  //   o PC e o modo.
  // como não tem suporte a processos, está carregando os valores dos
  //   registradores diretamente para a memória, de onde a CPU vai carregar
  //   para os seus registradores quando executar a instrução RETI

  // coloca o programa init na memória
  processo_t *init = so_adiciona_processo(self, "init.maq");

  if (init->reg_pc != 100) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }

  // altera o PC para o endereço de carga
  mem_escreve(self->mem, IRQ_END_PC, self->processo_corrente->reg_pc);
  // passa o processador para modo usuário
  mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em IRQ_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  int err_int;
  // t1: com suporte a processos, deveria pegar o valor do registrador erro
  //   no descritor do processo corrente, e reagir de acordo com esse erro
  //   (em geral, matando o processo)
  mem_le(self->mem, IRQ_END_erro, &err_int);
  err_t err = err_int;
  console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
  self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self)
{
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK) {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }
  // t1: deveria tratar a interrupção
  //   por exemplo, decrementa o quantum do processo corrente, quando se tem
  //   um escalonador com quantum
  // console_printf("SO: interrupção do relógio (não tratada)");
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static processo_t *so_adiciona_processo(so_t *self, char *nome_do_executavel);

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t1: com processos, o reg A tá no descritor do processo corrente
  int id_chamada;
  if (mem_le(self->mem, IRQ_END_A, &id_chamada) != ERR_OK) {
    console_printf("SO: erro no acesso ao id da chamada de sistema");
    self->erro_interno = true;
    return;
  }
  console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      break;
    case SO_MATA_PROC:
      so_chamada_mata_proc(self);
      break;
    case SO_ESPERA_PROC:
      so_chamada_espera_proc(self);
      break;
    default:
      console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
      // t1: deveria matar o processo
      self->erro_interno = true;
  }
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
  // implementação com espera ocupada
  //   T1: deveria realizar a leitura somente se a entrada estiver disponível,
  //     senão, deveria bloquear o processo.
  //   no caso de bloqueio do processo, a leitura (e desbloqueio) deverá
  //     ser feita mais tarde, em tratamentos pendentes em outra interrupção,
  //     ou diretamente em uma interrupção específica do dispositivo, se for
  //     o caso
  // implementação lendo direto do terminal A
  //   T1: deveria usar dispositivo de entrada corrente do processo

  int terminal = self->processo_corrente->terminal;

  int estado;
  if (es_le(self->es, terminal_processo(terminal, TECLADO_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }
  if (estado == 0){
    console_printf("SO: teclado não disponível");
    self->processo_corrente->estado = BLOQUEADO;
    return;
  }

  int dado;
  if (es_le(self->es, terminal_processo(terminal, TECLADO), &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }
  // escreve no reg A do processador
  // (na verdade, na posição onde o processador vai pegar o A quando retornar da int)
  // T1: se houvesse processo, deveria escrever no reg A do processo
  // T1: o acesso só deve ser feito nesse momento se for possível; se não, o processo
  //   é bloqueado, e o acesso só deve ser feito mais tarde (e o processo desbloqueado)
  mem_escreve(self->mem, IRQ_END_A, dado);
  self->processo_corrente->estado = PRONTO;
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
  // implementação com espera ocupada
  //   T1: deveria bloquear o processo se dispositivo ocupado
  // implementação escrevendo direto do terminal A
  //   T1: deveria usar o dispositivo de saída corrente do processo

  int terminal = self->processo_corrente->terminal;

 
  int estado;

  if (es_le(self->es, terminal_processo(terminal, TELA_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }
  if (estado == 0){
    console_printf("SO: tela não disponível");
    self->processo_corrente->estado = BLOQUEADO;
    return;
  }
  else {
    int dado;

    mem_le(self->mem, IRQ_END_X, &dado);
    if (es_escreve(self->es, terminal_processo(terminal, TELA), dado) != ERR_OK) {
      console_printf("SO: problema no acesso à tela");
      self->erro_interno = true;
      return;
    }

    self->processo_corrente->estado = PRONTO;
    mem_escreve(self->mem, IRQ_END_A, 0);
  }
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self)
{
  // ainda sem suporte a processos, carrega programa e passa a executar ele
  // quem chamou o sistema não vai mais ser executado, coitado!
  // T1: deveria criar um novo processo

  processo_t *proc = self->processo_corrente;

  if (proc == NULL) return;

  // em X está o endereço onde está o nome do arquivo
  // t1: deveria ler o X do descritor do processo criador

  int ender_proc = proc->reg_x;
  char nome[100];

  if (copia_str_da_mem(100, nome, self->mem, ender_proc)) {
    console_printf("SO: criando processo com programa '%s'", nome);

    processo_t *novo = so_adiciona_processo(self, nome);

    if (novo != NULL) {
      // t1: deveria escrever no PC do descritor do processo criado
      proc->reg_a = novo->process_id;
      return;
    } // else?
  }
  
  // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
  //   do processo que pediu a criação
  proc->reg_a = -1;
  mem_escreve(self->mem, IRQ_END_A, -1);
}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
  // T1: deveria matar um processo
  // ainda sem suporte a processos, retorna erro -1
  processo_t *proc = self->processo_corrente;

  if (proc == NULL) return;

  int pid_matar = proc->reg_x;

  processo_t *aux = busca_processo(self, pid_matar);

  console_printf("SO: processo %d vai matar processo %d", proc->process_id, pid_matar);

  if (pid_matar == 0){
    aux = self->processo_corrente;
    mata_processo(self, aux->process_id);
    console_printf("SO: processo morto, aqui!");
    proc->reg_a = 0;
  }
  else if (aux != NULL){
    mata_processo(self, pid_matar);
    console_printf("SO: processo %d morto, acá", pid_matar);
    proc->reg_a = 0;
  }
  else{
    proc->reg_a = -1;
    mem_escreve(self->mem, IRQ_END_A, -1);
  }
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
  // T1: deveria bloquear o processo se for o caso (e desbloquear na morte do esperado)
  // ainda sem suporte a processos, retorna erro -1
  processo_t *proc = self->processo_corrente;
  int id_alvo = proc->reg_x;

  processo_t *alvo = busca_processo(self, id_alvo);

  if (alvo == NULL || alvo == proc){
    proc->reg_a = -1;
    return;
  }

  if (alvo->estado != MORTO){
    proc->estado = BLOQUEADO;
  }
  else {
    proc->estado = PRONTO;
  }

  proc->reg_a = 0;
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_ini = prog_end_carga(prog);
  int end_fim = end_ini + prog_tamanho(prog);

  for (int end = end_ini; end < end_fim; end++) {
    if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }

  prog_destroi(prog);
  console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);
  return end_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// T1: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
      return false;
    }
    if (caractere < 0 || caractere > 255) {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0) {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}

// vim: foldmethod=marker
