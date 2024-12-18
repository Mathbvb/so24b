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
#include <stdio.h>

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50   // em instruções executadas
#define TAM_TABELA_PROCESSOS 8

// define qual o escalonador a ser usado: 0 para simples, 1 para round-robin e 2 para prioridade
#define ESCALONADOR 1
#define QUANTUM 5

typedef struct processo_t processo_t;
typedef struct metricas_so_t metricas_so_t;
typedef struct metricas_processo_t metricas_processo_t;

static void so_escalona_simples(so_t *self);
static void so_escalona_round_robin(so_t *self);
static void so_escalona_prioridade(so_t *self);

typedef void (*escalonador_func_t)(so_t *self);
escalonador_func_t escalonadores[] = {so_escalona_simples, so_escalona_round_robin, so_escalona_prioridade};

typedef enum {
  MORTO,
  BLOQUEADO,
  PRONTO,
  N_ESTADOS
} process_estado_t;

typedef enum {
  ESCRITA,
  LEITURA,
  ESPERANDO_MORRER,
  OK
} process_r_bloq_t;


// structs metricas
struct metricas_so_t {
  int t_total;
  int t_ocioso;
  int n_interrupcoes[N_IRQ];
  int preempcoes;
};

struct metricas_processo_t {
  int t_retorno;
  int preempcoes;
  int n_estados[N_ESTADOS];
  int t_estados[N_ESTADOS];
  int t_resposta;
};

struct processo_t {
  int process_id;
  int reg_a;
  int reg_x;
  int reg_pc;

  process_estado_t estado;
  process_r_bloq_t razao;

  int terminal;

  float prioridade;

  metricas_processo_t metricas;
};

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  es_t *es;
  console_t *console;
  bool erro_interno;

  processo_t *processo_corrente;
  processo_t **tabela_processos;
  int id_processo;
  int qnt_processos;
  int max_processos;

  int quantum;
  processo_t **fila_prontos;

  int relogio;
  metricas_so_t metricas;
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


// FUNÇÕES DE METRICAS {{{1
static void inicializa_metricas_processo(metricas_processo_t *metricas){
  metricas->t_retorno = 0;
  metricas->preempcoes = 0;
  for (int i = 0; i < N_ESTADOS; i++){
    metricas->n_estados[i] = 0;
    metricas->t_estados[i] = 0;
  }
  metricas->t_resposta = 0;

  metricas->n_estados[PRONTO] = 1;
}

static void atualiza_metricas_processo(processo_t *proc, int d_tempo){
  if (proc->estado != MORTO){
    proc->metricas.t_retorno += d_tempo;
  }
  proc->metricas.t_estados[proc->estado] += d_tempo;
  proc->metricas.t_resposta = proc->metricas.t_estados[PRONTO] / proc->metricas.n_estados[PRONTO];
}

static void inicializa_metricas_so(metricas_so_t *metricas){
  metricas->t_total = 0;
  metricas->t_ocioso = 0;
  metricas->preempcoes = 0;
  for (int i = 0; i < N_IRQ; i++){
    metricas->n_interrupcoes[i] = 0;
  }
}

static void atualiza_metricas_so(so_t *self, int d_tempo){
  self->metricas.t_total += d_tempo;
  if (self->processo_corrente == NULL){
    self->metricas.t_ocioso += d_tempo;
  }
  for (int i = 0; i < self->qnt_processos; i++){
    atualiza_metricas_processo(self->tabela_processos[i], d_tempo);
  }
}

static void calcula_metricas(so_t *self){
  int relogio_ant = self->relogio;
  if (es_le(self->es, D_RELOGIO_INSTRUCOES, &self->relogio) != ERR_OK){
    console_printf("SO: problema no acesso ao relógio");
    return;
  }

  if (relogio_ant != -1){
    int dif = self->relogio - relogio_ant;
    atualiza_metricas_so(self, dif);
  }
}

int calcula_preempcoes(so_t *self){
  int preempcoes = 0;
  for (int i = 0; i < self->qnt_processos; i++){
    preempcoes += self->tabela_processos[i]->metricas.preempcoes;
  }
  return preempcoes;
}

static void imprime_metricas(so_t *self){
  self->metricas.preempcoes = calcula_preempcoes(self);
  char nome[100];
  sprintf(nome, "../Metricas/metricas_so_%d.txt", ESCALONADOR);
  FILE *arq = fopen(nome, "w");
  if (arq == NULL){
    console_printf("SO: problema na abertura do arquivo de métricas");
    return;
  }
  fprintf(arq, "MÉTRICAS DO SO:\n\n");
  fprintf(arq, "Tempo total: %d\n", self->metricas.t_total);
  fprintf(arq, "Tempo ocioso: %d\n", self->metricas.t_ocioso);
  fprintf(arq, "Número de processos: %d\n", self->qnt_processos);
  fprintf(arq, "Preempções: %d\n", self->metricas.preempcoes);

  for (int i = 0; i < N_IRQ; i++){
    fprintf(arq, "Interrupção %d: %d\n", i, self->metricas.n_interrupcoes[i]);
  }

  fprintf(arq, "\nMÉTRICAS DOS PROCESSOS:\n\n");
  for (int i = 0; i < self->qnt_processos; i++){
    processo_t *proc = self->tabela_processos[i];
    fprintf(arq, "Processo %d\n", proc->process_id);
    fprintf(arq, "Tempo de retorno: %d\n", proc->metricas.t_retorno);
    fprintf(arq, "Preempções: %d\n", proc->metricas.preempcoes);
    fprintf(arq, "Tempo de resposta: %d\n", proc->metricas.t_resposta);
    for (int j = 0; j < N_ESTADOS; j++){
      fprintf(arq, "Tempo no estado %d: %d\n", j, proc->metricas.t_estados[j]);
      fprintf(arq, "Número de vezes no estado %d: %d\n", j, proc->metricas.n_estados[j]);
    }
    fprintf(arq, "\n");
  }
}

static processo_t *cria_processo(int process_id, int reg_pc){
  processo_t *processo = malloc(sizeof(processo_t));

  processo->process_id = process_id;

  processo->reg_a = 0;
  processo->reg_x = 0;
  processo->reg_pc = reg_pc;

  processo->estado = PRONTO;
  processo->razao = OK;

  processo->prioridade = 0.5;

  processo->terminal = (process_id % 4) * 4;

  inicializa_metricas_processo(&processo->metricas);

  return processo;
}

int tam_fila(so_t *self){
  int i = 0;
  while (self->fila_prontos[i] != NULL){
    i++;
  }
  return i;
}

static void remove_fila(so_t *self, int process_id){
  processo_t *aux;
  for (int i = 0; i < tam_fila(self); i++){
    aux = self->fila_prontos[i];
    if (aux->process_id == process_id){
      for (int j = i; j < tam_fila(self) - 1; j++){
        self->fila_prontos[j] = self->fila_prontos[j+1];
      }
      self->fila_prontos[tam_fila(self) - 1] = NULL;
      return;
    }
  }
}

static void mata_processo(so_t *self, int process_id){
  if (process_id <= 0 || process_id > self->qnt_processos) return;

  processo_t *proc = self->tabela_processos[process_id-1];
  proc->estado = MORTO;
  proc->metricas.n_estados[proc->estado]++;

  remove_fila(self, process_id);
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
    self->fila_prontos = realloc(self->fila_prontos, self->max_processos * sizeof(*self->fila_prontos));
  }

  processo_t *processo = cria_processo(self->id_processo, ender);

  self->tabela_processos[self->qnt_processos] = processo;
  self->fila_prontos[tam_fila(self)] = processo;

  self->qnt_processos++;
  self->id_processo++;

  return processo;
}

void muda_estado_processo(processo_t *proc, process_estado_t estado, process_r_bloq_t razao){
  proc->estado = estado;
  proc->razao = razao;
  proc->metricas.n_estados[proc->estado]++;
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

  self->id_processo = 1;
  self->qnt_processos = 0;
  self->max_processos = TAM_TABELA_PROCESSOS;
  self->processo_corrente = NULL;
  self->tabela_processos = malloc(self->max_processos * sizeof(processo_t *));

  self->fila_prontos = malloc(self->max_processos * sizeof(processo_t *));
  self->quantum = QUANTUM;

  self->relogio = -1;

  inicializa_metricas_so(&self->metricas);

  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

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

bool tudo_morreu(so_t *self){
  for (int i = 0; i < self->qnt_processos; i++){
    if (self->tabela_processos[i]->estado != MORTO){
      return false;
    }
  }
  return true;
}

static int finaliza_so(so_t *self){
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_TIMER, 0);
  e2 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);
  if (e1 != ERR_OK || e2 != ERR_OK)
  {
    console_printf("SO: nao consigo desligar o timer!!");
    self->erro_interno = true;
  }

  imprime_metricas(self);

  return 1;
}


static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;
 
  self->metricas.n_interrupcoes[irq]++;

  calcula_metricas(self);

  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);
  // faz o atendimento da interrupção
  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);
  // recupera o estado do processo escolhido

  if (!tudo_morreu(self)){
    return so_despacha(self);
  }
  else{
    return finaliza_so(self);
  }
}

static void so_salva_estado_da_cpu(so_t *self)
{
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

// função que ajusta a fila de processos, coloca o processo no fim da fila
void ajusta_fila(so_t *self, processo_t *proc){
  self->fila_prontos[tam_fila(self)] = proc;
}

void trata_le(so_t *self, processo_t *proc){
  int estado;
  int terminal = proc->terminal;

  if (es_le(self->es, terminal_processo(terminal, TECLADO_OK), &estado) == ERR_OK && estado != 0) {
    muda_estado_processo(proc, PRONTO, OK);
    ajusta_fila(self, proc);
    console_printf("SO: desbloqueado processo %d. haha - leitura", proc->process_id);

    int dado;
    es_le(self->es, terminal_processo(terminal, TECLADO), &dado);
    proc->reg_a = dado;
  }
  return;
}

void trata_escreve(so_t *self, processo_t *proc){
  int estado;
  int terminal = proc->terminal;

  if (es_le(self->es, terminal_processo(terminal, TELA_OK), &estado) == ERR_OK && estado != 0) {
    int dado = proc->reg_x;
    if (es_escreve(self->es, terminal_processo(terminal, TELA), dado) == ERR_OK) {
    muda_estado_processo(proc, PRONTO, OK);
      ajusta_fila(self, proc);
      console_printf("SO: desbloqueado processo %d. haha - escrita", proc->process_id);
    }
  }
  return;
}

static void trata_espera(so_t *self, processo_t *proc){
  int id_alvo = proc->reg_x;

  processo_t *alvo = busca_processo(self, id_alvo);

  if (alvo->estado == MORTO){
    muda_estado_processo(proc, PRONTO, OK);
    ajusta_fila(self, proc);
    console_printf("SO: desbloqueado processo %d. haha - espera", proc->process_id);
    return;
  }
}

static void so_trata_pendencias(so_t *self)
{
  for (int i = 0; i < self->qnt_processos; i++){
    processo_t *proc = self->tabela_processos[i];

    if (proc->estado == BLOQUEADO){
      int razao = proc->razao;

      switch (razao){
        case LEITURA:
          trata_le(self, proc);
          break;
        case ESCRITA:
          trata_escreve(self, proc);
          break;
        case ESPERANDO_MORRER:
          trata_espera(self, proc);
          break;
        
        default:
          break;
      }      
    }
  }

}

void calcula_prioridade(so_t *self, processo_t *proc){
  if (proc == NULL) return;
  proc->prioridade = (proc->prioridade + (QUANTUM - self->quantum) / (float)QUANTUM) / 2;
}

static void so_escalona(so_t *self){

  calcula_prioridade(self, self->processo_corrente);

  if(ESCALONADOR >= 0 && ESCALONADOR < 3){
    escalonadores[ESCALONADOR](self);
  }
  else{
    console_printf("SO: escalonador não implementado.");
    self->erro_interno = true;
  }
}

processo_t *proximo(so_t *self){
  for (int i = 0; i < self->qnt_processos; i++){
    if (self->tabela_processos[i]->estado == PRONTO){
      return self->tabela_processos[i];
    }
  }
  return NULL;
}

bool tem_bloqueado(so_t *self){
  for (int i = 0; i < self->qnt_processos; i++){
    if (self->tabela_processos[i]->estado == BLOQUEADO){
      return true;
    }
  }
  return false;
}

static void so_escalona_simples(so_t *self)
{
  if (self->processo_corrente != NULL && self->processo_corrente->estado == PRONTO) {
    return;
  }

  processo_t *prox = proximo(self);
  if (prox != NULL){
    self->processo_corrente = prox;
    return;
  }

  if (tem_bloqueado(self)){
    self->processo_corrente = NULL;
  }
  else{
    console_printf("SO: todos processos foram executados.");
    self->erro_interno = true;
  }
}

void ajusta_fila_pronto(so_t *self, processo_t *proc){
  remove_fila(self, proc->process_id);
  ajusta_fila(self, proc);
  self->processo_corrente = NULL;
}

static void so_escalona_round_robin(so_t *self){
  if (self->processo_corrente != NULL && self->processo_corrente->estado == PRONTO && self->quantum > 0){
    return;
  }

  if (self->processo_corrente != NULL && self->processo_corrente->estado == PRONTO && self->quantum == 0){
    self->processo_corrente->metricas.preempcoes++;
    ajusta_fila_pronto(self, self->processo_corrente);
  }

  if (tam_fila(self) != 0) {
    self->processo_corrente = self->fila_prontos[0];
    self->quantum = QUANTUM;
    return;
  }

  if (tem_bloqueado(self)){
    self->processo_corrente = NULL;
  }
  else{
    console_printf("SO: todos processos foram executados.");
    self->erro_interno = true;
  }
}

void ordena_fila_prioridade(so_t *self){
  processo_t *aux;
  for (int i = 0; i < tam_fila(self); i++){
    for (int j = i + 1; j < tam_fila(self); j++){
      if (self->fila_prontos[i]->prioridade > self->fila_prontos[j]->prioridade){
        aux = self->fila_prontos[i];
        self->fila_prontos[i] = self->fila_prontos[j];
        self->fila_prontos[j] = aux;
      }
    }
  }
}

static void so_escalona_prioridade(so_t *self){
  if (self->processo_corrente != NULL && self->processo_corrente->estado == PRONTO && self->quantum > 0){
    return;
  }

  if (self->processo_corrente != NULL && self->processo_corrente->estado == PRONTO && self->quantum == 0){
    self->processo_corrente->metricas.preempcoes++;
    ajusta_fila_pronto(self, self->processo_corrente);
  }

  if (tam_fila(self) != 0) {
    ordena_fila_prioridade(self);
    self->processo_corrente = self->fila_prontos[0];
    self->quantum = QUANTUM;
    return;
  }

  if (tem_bloqueado(self)){
    self->processo_corrente = NULL;
  }
  else{
    console_printf("SO: todos processos foram executados.");
    self->erro_interno = true;
  }
}

static int so_despacha(so_t *self)
{
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
  // coloca o programa init na memória
  processo_t *init = so_adiciona_processo(self, "init.maq");

  if (init->reg_pc != 100) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }

  // passa o processador para modo usuário
  mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  int err_int;

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

  if (self->quantum > 0){
    self->quantum--;
  }
  console_printf("SO: quantum do processo: %d", self->quantum);
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
      self->erro_interno = true;
  }
}

// implementação da chamada se sistema SO_LE
static void so_chamada_le(so_t *self)
{
  int terminal = self->processo_corrente->terminal;

  int estado;
  if (es_le(self->es, terminal_processo(terminal, TECLADO_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }
  if (estado == 0){
    console_printf("SO: teclado não disponível");
    muda_estado_processo(self->processo_corrente, BLOQUEADO, LEITURA);
    calcula_prioridade(self, self->processo_corrente);
    remove_fila(self, self->processo_corrente->process_id);
    return;
  }

  int dado;
  if (es_le(self->es, terminal_processo(terminal, TECLADO), &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }

  self->processo_corrente->reg_a = dado;
}

// implementação da chamada se sistema SO_ESCR
static void so_chamada_escr(so_t *self)
{
  int terminal = self->processo_corrente->terminal;

 
  int estado;

  if (es_le(self->es, terminal_processo(terminal, TELA_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }
  if (estado == 0){
    console_printf("SO: tela não disponível");
    muda_estado_processo(self->processo_corrente, BLOQUEADO, ESCRITA);
    calcula_prioridade(self, self->processo_corrente);
    remove_fila(self, self->processo_corrente->process_id);
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

    self->processo_corrente->reg_a = 0;
  }
}

// implementação da chamada se sistema SO_CRIA_PROC
static void so_chamada_cria_proc(so_t *self)
{
  processo_t *proc = self->processo_corrente;

  if (proc == NULL) return;

  int ender_proc = proc->reg_x;
  char nome[100];

  if (copia_str_da_mem(100, nome, self->mem, ender_proc)) {
    console_printf("SO: criando processo com programa '%s'", nome);

    processo_t *novo = so_adiciona_processo(self, nome);

    if (novo != NULL) {
      proc->reg_a = novo->process_id;
      return;
    } 
  }
  proc->reg_a = -1;
}

// implementação da chamada se sistema SO_MATA_PROC
static void so_chamada_mata_proc(so_t *self)
{
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
  }
}

// implementação da chamada se sistema SO_ESPERA_PROC
static void so_chamada_espera_proc(so_t *self)
{
  processo_t *proc = self->processo_corrente;
  int id_alvo = proc->reg_x;

  processo_t *alvo = busca_processo(self, id_alvo);

  if (alvo == NULL || alvo == proc){
    proc->reg_a = -1;
    return;
  }

  if (alvo->estado != MORTO){
    muda_estado_processo(proc, BLOQUEADO, ESPERANDO_MORRER);
    calcula_prioridade(self, proc);
    remove_fila(self, proc->process_id);
    return;
  }

  muda_estado_processo(proc, PRONTO, OK);
  proc->reg_a = 0;
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
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