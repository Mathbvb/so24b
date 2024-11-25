# Relatório - t1

## Introdução

O objetivo deste trabalho foi avaliar o desempenho de três algoritmos de escalonamento: **Simples**, **Round Robin** e **Baseado em Prioridade**. Para cada algoritmo, foram analisadas métricas no SO relacionadas a quantidade de processos, tempo de execução, ociosidade, interrupções, preempções e números de interrupções recebidas de cada tipo. Além disso também há indicadores de desempenho específicos para cada processo, como tempos de retorno, resposta, preempções, tempo e número de vezes que o processo ficou em cada estado.

## Escalonadores

Para a realização do trabalho foram implementados os três seguintes escalonadores, com características únicas para a escolha do processo a ser executado.

- **Simples:** O escalonador simples executa um processo até que seu estado enquanto seu estado continua pronto, uma vez que o processo executando morre ou é bloqueado o escalonador seleciona o primeiro processo de estado pronto que encontrar na tabela de processos.

- **Round-Robin:** Para a implementação do escalonador round-robin foi implementada uma fila de processos prontos e um quantum que representa a quantidade de interrupções de relógio que um processo pode sofrer sem sair da execução. Quando desbloqueado ou quando seu quantum termina (preempção) o processo é inserido no fim da fila. O escalonador sempre seleciona o processo na primeira posição da fila para executar.

- **Prioridade:** O escalonador com base em prioridade funciona de maneira similar ao round-robin, ele seleciona o processo a ser executado em uma fila de processos prontos e seus processos também possuem um quantum Contudo, ele difere do circular pois na escolha de processo, que é baseada no processo com maior prioridade (processo com o menor valor na variável prioridade).

## Métricas

Métricas gerais: Quantum = 5 e intervalo = 50.

### Métricas do escalonador simples:

#### SO:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Número de processos       | 4          |
| Tempo total de execução   | 27941      |
| Tempo total ocioso        | 8562       |
| Preempções                | 0          |

| INTERRUPÇÃO      | OCORRÊNCIAS |
|------------------|-------------|
| Reset            | 1           |
| CPU              | 0           |
| Sistema          | 462         |
| Relógio          | 557         |
| Teclado          | 0           |
| Tela             | 0           |

#### Processo 1:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 27941      |
| Tempo de resposta         | 185        |
| Preempções                | 0          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 0           |
| BLOQUEADO | 3           | 27198       |
| PRONTO    | 4           | 743         |

#### Processo 2:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 13008      |
| Tempo de resposta         | 1797       |
| Preempções                | 0          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 14550       |
| BLOQUEADO | 6           | 424         |
| PRONTO    | 7           | 12584       |

#### Processo 3:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 15831      |
| Tempo de resposta         | 653        |
| Preempções                | 0          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 11719       |
| BLOQUEADO | 21          | 1462        |
| PRONTO    | 22          | 14369       |

#### Processo 4:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 27223      |
| Tempo de resposta         | 139        |
| Preempções                | 0          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 319         |
| BLOQUEADO | 130         | 8970        |
| PRONTO    | 131         | 18253       |

O escalonador simples apresentou um tempo total de execução igual a 27941 e um tempo ocioso de 8562. Executar os processos de maneira sequencial, sem preempção, como esse escalonador faz pode aumentar consideravel o tempo ocioso durante a execução.

### Métricas do escalonador Round-Robin:

#### SO:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Número de processos       | 4          |
| Tempo total de execução   | 24049      |
| Tempo total ocioso        | 4610       |
| Preempções                | 58         |

| INTERRUPÇÃO      | OCORRÊNCIAS |
|------------------|-------------|
| Reset            | 1           |
| CPU              | 0           |
| Sistema          | 462         |
| Relógio          | 479         |
| Teclado          | 0           |
| Tela             | 0           |

#### Processo 1:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 24049      |
| Tempo de resposta         | 185        |
| Preempções                | 2          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 0           |
| BLOQUEADO | 2           | 23306       |
| PRONTO    | 4           | 743         |

#### Processo 2:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 17097      |
| Tempo de resposta         | 1833       |
| Preempções                | 39         |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 6569        |
| BLOQUEADO | 8           | 592         |
| PRONTO    | 9           | 16505       |

#### Processo 3:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 11989      |
| Tempo de resposta         | 797        |
| Preempções                | 12         |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 11669       |
| BLOQUEADO | 13          | 830         |
| PRONTO    | 14          | 11159       |

#### Processo 4:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 23331      |
| Tempo de resposta         | 155        |
| Preempções                | 5          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 319         |
| BLOQUEADO | 104         | 6979        |
| PRONTO    | 105         | 16352       |

O escalonador Round-Robin apresentou uma um tempo total de execução igual a 24049 e um tempo ocioso de 4610. O que demonstra que o escalonador distribui de maneira mais eficaz o uso da CPU para os processos.

### Métricas do escalonador Prioridade:

#### SO:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Número de processos       | 4          |
| Tempo total de execução   | 22896      |
| Tempo total ocioso        | 3445       |
| Preempções                | 59         |

| INTERRUPÇÃO      | OCORRÊNCIAS |
|------------------|-------------|
| Reset            | 1           |
| CPU              | 0           |
| Sistema          | 462         |
| Relógio          | 456         |
| Teclado          | 0           |
| Tela             | 0           |

#### Processo 1:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 22896      |
| Tempo de resposta         | 185        |
| Preempções                | 2          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 0           |
| BLOQUEADO | 2           | 23306       |
| PRONTO    | 4           | 743         |

#### Processo 2:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 17744      |
| Tempo de resposta         | 1911       |
| Preempções                | 39         |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 4769        |
| BLOQUEADO | 8           | 537         |
| PRONTO    | 9           | 17207       |

#### Processo 3:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 11728      |
| Tempo de resposta         | 760        |
| Preempções                | 13         |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 10777       |
| BLOQUEADO | 13          | 1082        |
| PRONTO    | 14          | 10646       |

#### Processo 4:

| MÉTRICA                   | VALOR      |
|---------------------------|------------|
| Tempo de retorno          | 22178      |
| Tempo de resposta         | 162        |
| Preempções                | 5          |

|ESTADO     | N° DE VEZES | TEMPO TOTAL |
|-----------|-------------|-------------|
| MORTO     | 1           | 319         |
| BLOQUEADO | 97          | 6217        |
| PRONTO    | 98          | 15961       |

O escalonador baseado em prioridade foi o mais eficiente de todos, apresentando um tempo total de execução de 22896 e um tempo ocioso de 3445. Essa efiência advém do modo com o qual são escolhidos os processos, fazendo com que os recursos da CPU sejam alocados para os processos mais necessitados.

## Considerações finais

Os resultados obtidos demonstram diferenças significativas entre os três algoritmos de escalonamento avaliados, tanto em relação a efiência quanto ao tempo total como também quanto ao tempo ocioso.

O Escalonador Simples apresentou o maior tempo de execução e ociosidade, o que o demonstra como uma abordagem menos eficiente para a distribuição de recursos. 

O Round-Robin mostrou-se mais eficiente que o escalonador simples reduzindo o tempo total de execução e de ociosidade, contudo ele também apresentou um número consideravelmente alto de preempções o que pode gerar sobrecarga do sistema em ambientes com muitos processos. 

Por fim, o Escalonador de Prioridade apresentou os melhores resultados dentre os três escalonadores testados, demonstrando a eficiência de priorizar a execução de processos com maior necessidade. Porém, assim como o Round-Robin, também apresentou um número consideravel de preempções.

Com base nos resultados obtidos, é possível concluir que a escolha do algoritmo de escalonamento tem um impacto significativo e direto na eficiência de um SO. Assim, para conseguir máxima eficiência devem ser observados o contexto e os requisitos de um ambiente para determinar a melhor estratégia a ser utilizada.