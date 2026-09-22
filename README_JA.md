# ZMK Input Processors

[![Test](https://github.com/amgskobo/zmk-input-processors/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-processors/actions/workflows/test.yml)

[English](README.md)

ZMK 標準の input processor を、実行中に設定変更できるようにしたものです。

ZMK には設定可能な input processor が 4 つ(scaler、transform、code-mapper、
temp-layer)ありますが、そのパラメーターは devicetree からビルド時に flash へ
焼き込まれます。そのため、ポインターの速度やパッドの向きを変えるには再ビルドが
必要です。この module は同じ 4 つの processor を、パラメーターを RAM に持つ形で
提供します。名前はすべて `runtime-*` で始まり、chain を見ただけで実行中に変更
できる stage が分かります。

動作に必要なのは upstream ZMK だけです。追加の仕組みが必要なのは実行時の
*アクセス* の側です。ZMK には processor のパラメーターへ届く protocol がないため、
その部分は
[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
があるときだけコンパイルされるファイルに分離しています。これがなくても processor は
ビルド・動作し、devicetree の値で固定されます。

独自の processor は、それぞれ別の module にあります。
[abs2rel](https://github.com/amgskobo/zmk-input-abs2rel)、
[vector-acceleration](https://github.com/amgskobo/zmk-input-vector-acceleration)、
[inertia](https://github.com/amgskobo/zmk-input-inertia)、
[padstick](https://github.com/amgskobo/zmk-input-padstick)。

## Features

- **Runtime Scaler** — node に保持した比率で pointer event を 64-bit 演算で拡大縮小
- **Runtime Transform** — node に保持した 3 つの flag で軸の入れ替え・反転
- **Runtime Code Mapper** — event code を書き換え。map 全体を無効にする switch 付き
- **Runtime Temp Layer** — pointer 使用中にレイヤーを上げる。レイヤーとタイマーは node に保持
- 新しい input processor を追加しやすい module 構成
- devicetree による設定
- Kconfig による条件付きビルド

## 互換性

- scaler、transform、code mapper は単体構成と split 構成の両方で動作します。
  input listener を持つ peripheral 側でも使えます
- temp-layer processor は keymap のレイヤー状態を変更するため central 専用です
- `CONFIG_ZMK_POINTING` の有効化が必要です

## インストール

この module は Zephyr module として使います。ZMK application に追加してください。

### 方法 1: `west.yml` を使う

application の `west.yml` に追加します。

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-processors
      remote: amgskobo
      revision: main
```

その後、次を実行します。
```bash
west update
```

### 方法 2: ローカルの checkout を使う

`ZMK_EXTRA_MODULES` でローカルの checkout を ZMK のビルドへ渡します。

```bash
west build -b <board> -s zmk/app -- -DZMK_EXTRA_MODULES=/path/to/zmk-input-processors
```

`ZEPHYR_EXTRA_MODULES` ではありません。ZMK は自身の board や keymap 処理を
読み込むためにこの変数を自分で設定しており、環境変数やコマンドラインで値を
渡すと ZMK のリストに追加されるのではなく置き換わります。その結果、ビルドが
ZMK の board を見つけられず失敗します。

## 使い方

### module の既定 node

`<zmk-input-processors/input_processor_runtime.dtsi>` を include すると、
公式と同じ形式の、何も変更しない `zip_*` node が使えます。DYA の設定キー用に
短い node 名を使い、`/omit-if-no-ref/` 付きなので、input chain から参照された
ときだけ存在します。

| 参照 label | node 名 | 既定の対象 |
| :--- | :--- | :--- |
| `zip_runtime_xy_scaler` | `zip_rt_xy_scale` | 相対 X/Y、1/1 |
| `zip_runtime_xy_transform` | `zip_rt_xy_xform` | 相対 X/Y、変換なし |
| `zip_runtime_scroll_scaler` | `zip_rt_scr_scale` | WHEEL/HWHEEL、1/1 |
| `zip_runtime_scroll_transform` | `zip_rt_scr_xform` | WHEEL/HWHEEL、変換なし |
| `zip_runtime_scroll_mapper` | `zip_rt_scr_map` | Y→WHEEL、X→HWHEEL |
| `zip_runtime_temp_layer` | `zip_rt_tmp_layer` | レイヤー 0、timeout 400 ms、prior-idle guard 300 ms |

必要に応じて、board 側で temp-layer node の対象レイヤーと除外する物理位置を
上書きします。

## 命名

名前が `runtime` で始まる processor は、パラメーターを RAM に持ち、自分の node に
名前付きで保持します。そのため実行中に一覧・変更できます。`runtime` が付かない
ものはビルド時に固定されます。prefix の意味はそれだけですが、chain 上では両者の
見分けがつかないため、名前で示す価値があります。

```dts
input-processors = <&zip_xy_transform (INPUT_TRANSFORM_Y_INVERT)>,  /* fixed  */
                   <&scroll_xform>;                     /* live   */
```

各 runtime processor は upstream の `zip_*` processor に対応し、同じように
動作します。違いは event の処理経路ではありません。upstream は devicetree から
`static const` の config を作るか、chain のパラメーター cell を読みます。どちらも
flash 上にあり、client が名前で指定できるものではありません。特に chain cell は
「この listener の 4 番目の slot の 2 番目の数値」としか指定できず、chain を
編集した瞬間に別のものを指すようになります。

そのため、runtime processor はすべて `#input-processor-cells = <0>` を宣言し、
パラメーターを property として持ちます。chain の各項目は数値を取りません。

prefix は module 名と重複しているように見えますが、重複ではありません。upstream は
`ZMK_INPUT_PROCESSOR_SCALER`、`_TRANSFORM`、`_CODE_MAPPER`、`_TEMP_LAYER` を
所有しており、Kconfig は同じ symbol の 2 つの定義を拒否せずにマージします。
そのため `RUNTIME` を外すと、この node を宣言したときに upstream の driver *も*
有効になり、instance のないまま image に組み込まれます。ビルドは成功し、唯一の
兆候は ZMK のファイルで出る未使用変数の警告だけです。compatible 文字列からも
外せません。こちらは、2 つの binding が同じ compatible を共有するとビルドが
エラーになるという、より分かりやすい理由によります。

この module の processor はすべて固定の upstream processor を置き換えるため、
すべてに prefix が付いています。以前は付いていないもの(upstream に対応する
ものがない独自の absolute-to-relative)が 1 つあり、例外として文書化していましたが、
例外であること自体が問題の兆候だと分かりました。現在は他の独自 processor と
同じく [専用の module](https://github.com/amgskobo/zmk-input-abs2rel) にあります。

### パラメーターを移すことの代償

chain cell は slot に属し、property は node に属します。そのため upstream は
1 つの device を 2 つの slot に置き、それぞれに別の数値を与えられますが、
この module の processor ではできません。

```dts
/* upstream: one device, two ratios */
input-processors = <&zip_scaler 1 16>;      /* here */
input-processors = <&zip_scaler 4 1>;       /* and differently here */

/* here: one node is one set of values, wherever it appears */
input-processors = <&scroll_scale>;
input-processors = <&scroll_scale>;   /* the same speed, necessarily */
```

これはトレードオフですが、向きは正しいものです。名前のない値は編集できず、
slot ごとの数値はまさに名前を持たないものだからです。別の値が必要な 2 つの slot は
2 つの node を宣言します。数行の devicetree が増える代わりに、それぞれが自分の
キーを持てます。*同じ* 値が必要な 2 つの slot は、同じ値を保つことが保証されます。
2 つの chain cell では値がずれることがありました。

### すべての stage に no-op があることが重要

この module の processor は、自分の値を編集するだけで、すべての event を
そのまま通す状態にできます。

| Processor | そのまま通す条件 |
| :--- | :--- |
| `runtime-scaler` | `multiplier == divisor`(`multiplier == 0` では軸を止める) |
| `runtime-transform` | flag が 1 つも設定されていない |
| `runtime-code-mapper` | `enabled` が false |
| `runtime-temp-layer` | `enabled` が false |

これが、再ビルドせずに chain を編集できる理由です。device は devicetree が作り、
保存領域はすでに存在する device しか参照できないため、chain にない stage を後から
有効にはできません。しかし chain に *ある* stage は、何もしない値にしておけば
ほとんどコストがかからず、いつでも有効にできます。使うかもしれない stage を
no-op としてあらかじめ置いておけば、実行時に並べ替えられる chain で得たいことの
大部分が得られます。

並べ替えについても、実際に必要な形で実現できます。同じ種類の stage を 2 か所に
置き、値をその間で移します。

```dts
input-processors = <&zip_absolute_to_relative>,  /* zmk-input-abs2rel */
                   <&pointer_scale_pre>,         /* 1/1 — before the curve */
                   <&vector_accel>,
                   <&pointer_scale>;       /* 889/500 — after it */
```

「加速カーブの後ではなく前で拡大縮小する」変更は、ファームウェアの変更ではなく
2 回の編集になります。任意の並べ替えには位置ごとに 1 つずつ配置が必要になり
現実的ではありませんが、実際に必要な 2〜3 通りの順序なら可能です。

コストは本当に小さいものです。flag のない transform は code リストを走査しません
(反転の判定が flag で短絡します)。1/1 の scaler は remainder を 0 のまま
保ちます。配置済みの stage を持つすべての chain がこの性質に依存するため、
host テストで固定しています。

事前配置で得られないのは、存在しない stage です。任意角度の回転や絶対座標モードは、
まず processor を実装する必要があります。実装して配置すれば、そのつまみは
設定項目になります。

### 複数 instance

この module の processor は状態をそれぞれの `data` 構造体に持ち、temp-layer の
work item も instance ごとなので、instance 同士は干渉しません。それでも
知っておくべき点が 2 つあります。

- **設定キーは通常 node 名で区別されます。** devicetree は兄弟 node の名前が
  異なることを保証しますが、親の異なる node 同士は同じ名前を持てます。module は
  起動時に公開する全キーを検査し、両方の instance が編集できるふりをせず、
  重複をログに出します。
- **同じレイヤーを指す 2 つの temp-layer instance は、そのレイヤーを共有します。**
  ZMK のレイヤーは参照カウントではなく bit なので、先に下げた方が両方の分を
  下げます。これは固着せず自然に解消します。レイヤー変更が両方に届き、それぞれが
  レイヤーを保持していないと認識し、次の移動で再び上がるため、失うのはレイヤー
  1 回分です。独立した寿命が必要なら、別のレイヤーを使ってください。

processor を追加する人向けにもう 1 点あります。devicetree の値に対する `#if` は、
その値が devicetree にある間しか正しくありません。temp-layer は、
`excluded-positions` が構造的な値なので position event を条件付きで購読します。
一方、upstream は keycode event の購読を `require-prior-idle-ms` で条件分けして
いますが、ここでは無条件に購読します。この値は実行時の値になったため、0 で
始まった instance にも client から実際の guard を与えられるからです。購読を
コンパイル時に外すと、その設定は編集を受け付けるのに何も起こらない状態になります。

### Runtime Scaler を有効にする

`zmk,input-processor-runtime-scaler` は、ZMK の `zmk,input-processor-scaler` と
同じく相対 pointer event を拡大縮小します。違いは 2 つです。

**演算は 64 bit で行います。** 標準の scaler は `event->value * multiplier` を
`int16_t` に保持するため、multiplier が 889 のとき 37 以上の delta は除算前に
負の値へ折り返し、最も速く動かしているときに pointer が逆方向へ飛びます。この式を
そのまま引き継いだ runtime scaler も、同じ反転を起こします。ここでは分子と商を
`int64_t` で扱い、範囲外の商は折り返さず飽和させるため、速い動きは速いまま
出力されます。

**比率は chain cell の組ではなく property です。** これが名前で指定できる理由です。
chain cell は「この listener の 4 番目の slot の 2 番目の数値」としか指定できず、
chain を編集するとすぐに別のものを指します。node の property には名前があるので、
公開・一覧・実行時の変更ができます。

```dts
/ {
    input_processors {
        pointer_scale: pointer_scale {
            compatible = "zmk,input-processor-runtime-scaler";
            #input-processor-cells = <0>;
            type = <INPUT_EV_REL>;
            codes = <INPUT_REL_X>, <INPUT_REL_Y>;
            multiplier = <889>;
            divisor = <500>;
            track-remainders;
        };
    };
};

&trackpad_listener {
    /* &zip_absolute_to_relative comes from zmk-input-abs2rel. */
    input-processors = <&zip_absolute_to_relative>, <&pointer_scale>;
};
```

chain の項目が数値を取らない点に注意してください。`track-remainders` は除算で
切り捨てられる端数を保持するためのもので、これにより 1 未満の比率でも pointer が
動きます。

#### 設定リファレンス

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | int | `INPUT_EV_REL` (2) | 対象とする event type。 |
| `codes` | array | *必須* | 拡大縮小する event code。それ以外は変更せず通します。 |
| `multiplier` | int | *必須* | 分子。0〜32767。0 で軸を止めます。 |
| `divisor` | int | *必須* | 分母。1〜32767。 |

どちらの範囲も、選んだ値ではなく正しさのための上限です。ZMK は追跡中の
remainder を `int16_t` の slot に保持しており、2 つの数値を正の `int16` の範囲に
収めることで、remainder がその slot に収まることを保証しています。範囲外の値は
`BUILD_ASSERT` によってビルドが失敗し、実行時にも拒否されます。

現在の divisor では生じ得ない範囲の remainder は破棄します。remainder は processor
ではなく listener の slot にあるため、実行時に比率を変更しても残ります。
互換性のない remainder を捨てることで、変更直後の最初の報告で大きく跳ぶことを
防ぎます。新しい divisor でも有効な remainder は次の報告に 1 count 未満の影響を
与えますが、これは remainder が本来運ぶべき丸め誤差です。

### Runtime Transform を有効にする

`zmk,input-processor-runtime-transform` は、ZMK の
`zmk,input-processor-transform` と同じく軸の入れ替えと反転を行います。upstream は
3 つの flag を chain パラメーター cell の bit として持ちますが、ここではそれぞれが
名前付きの property であり、client では 3 つの checkbox になります。逆向きに
取り付けたトラックパッドの補正が、再ビルドではなく設定になります。

```dts
scroll_xform: scroll_xform {
    compatible = "zmk,input-processor-runtime-transform";
    #input-processor-cells = <0>;
    type = <INPUT_EV_REL>;
    x-codes = <INPUT_REL_X>;
    y-codes = <INPUT_REL_Y>;
    y-invert;
};
```

#### 設定リファレンス

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | int | `INPUT_EV_REL` (2) | 対象とする event type。 |
| `x-codes` | array | *必須* | X 軸の code。`y-codes` と index で対応します。 |
| `y-codes` | array | *必須* | Y 軸の code。`x-codes` と同じ長さ。 |
| `xy-swap` | bool | false | 初期状態: X と Y の code を入れ替えます。 |
| `x-invert` | bool | false | 初期状態: X 軸の値を反転します。 |
| `y-invert` | bool | false | 初期状態: Y 軸の値を反転します。 |

入れ替えが先に実行されるため、反転は event が到着した軸ではなく、最終的に
移った先の軸に適用されます。これは upstream と同じ順序で、逆にすると 2 つの処理が
干渉します。入れ替えは index で対応付けるため、2 つの code リストは同じ長さで
なければならず、`BUILD_ASSERT` で検査します(upstream にもこの assert はありますが、
`x` 同士を比較しているため発動しません)。

### Runtime Code Mapper を有効にする

`zmk,input-processor-runtime-code-mapper` は、ZMK の
`zmk,input-processor-code-mapper` と同じく from/to の表で event code を書き換えます。
map 自体は構造的な値なので devicetree に残し、実行時に変更できるのは map を
適用するかどうかです。

この 1 つの switch が要点です。これを切ると、レイヤーも再ビルドも chain の変更も
なしに、scroll の経路を通常の pointer の経路へ戻せます。processor は消えるのでは
なく no-op になるため、他の stage は形も値もそのまま残り、再び有効にすれば経路が
正確に元へ戻ります。

```dts
scroll_map: scroll_map {
    compatible = "zmk,input-processor-runtime-code-mapper";
    #input-processor-cells = <0>;
    type = <INPUT_EV_REL>;
    map = <INPUT_REL_Y INPUT_REL_WHEEL>,
          <INPUT_REL_X INPUT_REL_HWHEEL>;
};
```

#### 設定リファレンス

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | int | `INPUT_EV_REL` (2) | 対象とする event type。 |
| `map` | array | *必須* | from/to の code の組を平坦に並べたもの。最初に一致したものを使います。 |
| `start-disabled` | bool | false | map を無効にした状態で起動します。 |

Zephyr の boolean は存在の有無で表すため、既定値を true にできません。そのため
devicetree の property は否定形になっています。client に表示される設定は肯定形の
`enabled` です。

### Runtime Temp Layer を有効にする

`zmk,input-processor-runtime-temp-layer` は、ZMK の
`zmk,input-processor-temp-layer` と同じく pointer の使用中にレイヤーを上げます。
移動でレイヤーが上がり、除外位置以外のキー押下で下がり、timeout で下がり、
直前のキー押下があるとそもそも上がりません。

upstream はレイヤーと timeout を chain cell から取りますが、ここでは 3 つの数値が
すべて node の property です。これらは pointer を 1 週間使った後に見直す値
(レイヤーをどれだけ保持するか、どれだけ直前のキー押下でブロックするか)であり、
試すたびに再ビルドが必要では、自分に合う値にたどり着けません。

```dts
pointer_layer: pointer_layer {
    compatible = "zmk,input-processor-runtime-temp-layer";
    #input-processor-cells = <0>;
    layer = <3>;
    timeout-ms = <400>;
    require-prior-idle-ms = <300>;
    excluded-positions = <7 8 9 13 14 23 24 25 26>;
};
```

#### 設定リファレンス

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `layer` | int | *必須* | 上げるレイヤー。レイヤー **ID** で指定します。 |
| `timeout-ms` | int | *必須* | pointer の無操作がこの時間続くとレイヤーを下げます。0〜60000 ms。0 は timeout なし。 |
| `require-prior-idle-ms` | int | 0 | キー押下後、この時間はレイヤーを上げません。0〜60000 ms。 |
| `excluded-positions` | array | *なし* | レイヤーを下げない位置。 |
| `start-disabled` | bool | false | 無効な状態で起動し、何も上げません。 |

`layer` は `LAYER_ID` 制約付きで公開されるため、client は keymap 自身のレイヤー
一覧を表示し、行は「3」ではなく「MOUSE (3)」のように表示されます。レイヤーは、
見ただけでは妥当か判断できない唯一の値だからです。制約を理解しない client 向けの
fallback として範囲も併せて宣言していますが、どちらも信用はしません。制約は
client に届く表示上のヒントであり、firmware が守られた前提にしてよい規則では
ないため、driver は keymap の範囲外のレイヤーと 60000 を超えるタイマーを常に
拒否します。

`start-disabled` があるのは、この stage に他の no-op がないからです。この module の
他の processor は、自分の値を編集するだけで event をそのまま通す状態にできます
(`multiplier == divisor` の scaler、flag のない transform)。これにより、再ビルド
ではなく数値の変更で chain の形を変えられます。timeout 0 はそうなりません。
「timeout で下げない」という意味であり、「上げない」ではないからです。そのため
switch を明示的に設けています。switch を切ると、この processor が保持している
レイヤーは、下げる責任を持つものがいなくなる前に下げられます。

レイヤーの有効化と timeout の処理は、input callback の中でも Zephyr の共有
system work queue でもなく、ZMK の low-priority work queue で実行します。worker は
何かを上げる前に、現在の有効状態と入力中 guard を再確認します。processor の無効化や
条件を満たさないキー押下も、キュー済みの有効化を無効にします。そのため古い work
item が、それを取り消した event の後にレイヤーを上げることはありません。
パラメーターの更新は、worker の lock を待つ前に generation counter も進めます。
worker は有効化の前後でこれを確認し、レイヤー event の配信中に編集が届いた場合は、
その有効化をただちに取り消します。

`excluded-positions` は構造的な値のままです。キー位置のリストは汎用の設定一覧では
表現できず、好みではなく物理レイアウトに属するものだからです。この property を
省略すると位置による解除はすべて無効になり、レイヤーを下げられるのは 0 以外の
timeout か processor を無効にしたときだけになります。

レイヤーが上がっている間に timeout を変更すると、設定を更新した時点から計測を
やり直します。0 にすると保留中の timeout を取り消します。パラメーター更新より前に
キューされた有効化は破棄されるため、古いパラメーターで観測した event が、その後に
古いレイヤーも新しいレイヤーも上げることはありません。

パラメーター以外にも、upstream との違いが 3 つあります。

- **work item は instance ごとです。** upstream はレイヤーを index とする global な
  配列を 1 つ持ち、work handler は `DEVICE_DT_INST_GET(0)` で device を解決する
  ため、2 つ目の instance が 1 つ目の状態を動かしてしまいます。
- **レイヤー番号を一貫して ID として扱います。** upstream は ID を取る
  `zmk_keymap_layer_activate(toggle_layer)` で有効化しますが、確認には
  `zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(toggle_layer))` を使い、
  index ではない値を変換しています。両者はレイヤーを並べ替えるまでは一致しますが、
  Studio client は並べ替えができます。
- **message queue を使いません。** 有効化と無効化は instance 上の通常の work item と
  delayable work item であり、queue とその深さを決める Kconfig が不要です。

### 実行時にパラメーターを変更する

`CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS=y` のとき、この module のすべての
パラメーターは
[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
を通じて公開されます。custom settings の一覧を表示する Studio client なら、
`amgskobo__rip` subsystem の下に表示され、宣言した型と範囲に応じた widget に
なります。この module 自身は画面も protocol も持ちません。

キーは、所有する node の devicetree 名に field を続けたものです。

```
zip_rt_xy_scale.multiplier
zip_rt_scr_scale.multiplier
zip_rt_tmp_layer.timeout_ms
fine_scroll_scale.multiplier
```

**キーそのものなので、node にはキーとして名前を付けてください。** 名前は client に
表示され、人が検索し、chain view が結合に使う prefix でもあります。
`<route>_<what it does>[_<which one>]` の形にすると、経路ごとの stage が並んで
表示され、短く保てます。

```
zip_rt_xy_scale     zip_rt_tmp_layer    zip_rt_scr_scale
zip_rt_scr_xform    zip_rt_scr_map      fine_scroll_scale
```

経路に下位の経路がある場合は、名前を短くするより階層化してください。関連する
stage が並べ替えた設定一覧でもまとまって表示されます。

**field は devicetree property の綴りではなく、値の意味で名付けます。** キーは
client が表示するラベルなので、`start-disabled` は否定形を UI に持ち込まず
`enabled` になります。`suppress-btn0` は語を省かず `suppress_btn0` のままです。
`btn0 = true` ではボタンが有効であるように読めますが、実際はボタンを取り除く
という意味だからです。48 byte の上限が求めていない省略はしません。`multiplier` を
`mul` にしないのはそのためです。

ここでの長さはスタイルの問題ではありません。この module はキーと保存名の両方の
上限をビルド時に検査します。`amgskobo__rip` subsystem と最長の field 名から、
node 名に使えるのは **19 文字** です。

option label(client が dropdown で node を表示するときの文字列)は、終端文字を
含めて別に 32 byte が上限で、超えると何も言わずに切り詰められます。19 文字の
node 名の規則は、永続化の安全な上限であると同時に、選択肢 label の上限にも
十分収まります。

キーの上限を超えるとビルドが失敗し、`ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS` が
node 名を示して失敗させます。

```
static assertion failed: "devicetree node "pointer_absolute_to_relative_far_too_long"
has a name too long to key its settings; shorten the node name"
```

custom-settings も同じ上限を検査しますが、自身の macro の中で行うため、どれかの
キーが長すぎたことしか報告できません。対処できるのは node だけです。

予算上必要な場合だけ node 名を省略してください。`abs_rel`、`accel`、`xform` は
慣用的な短縮形です。設定の field 名と参照 label は説明的なままにします。これらは
instance を区別するために board の作者が削る部分ではありません。

module 自身の dtsi にある singleton は、同梱時の名前で固定されます。その名前が
長すぎる、または意味が合わない場合は、dtsi を include せず board 側で node を
宣言してください。include した node は、module が `/omit-if-no-ref/` を付けて
いない限り、参照されなくてもビルドされ、キーも公開されます。

node 名を使うのは意図的で、client の作者が知らないキーボードでも機能する理由です。
chain を描く view は、各 listener の processor を devicetree からたどり、stage ごとに
`const struct device *` を得ます。その `->name` は `DEVICE_DT_NAME()`、つまり
`DT_NODE_FULL_NAME()` であり、キーを作るのと同じ文字列です。したがって stage の
設定は、ちょうどその device 名で始まるキーになり、そのために **module 間の登録や
取り決めも、board 作者が devicetree に書き込むことも必要ありません。** この規約に
従わない module は結合されないだけで、その設定は平坦な一覧には表示されます。

名前を手書きしないので、最もよくある衝突の原因はなくなります。ただし、すべては
防げません。`DT_NODE_FULL_NAME` はパスではなく node 自身の名前であり、devicetree は
兄弟間でしか一意性を保証しないため、`/input_processors` の下の board の node が
root にある module の node と同じ名前を持つことがあります。下流はこれを検出せず、
`zmk_custom_setting_find()` は最初に一致したものを返します。そのため module は
起動時に一度検査し、`Duplicate setting key "..."` をログに出します。

知っておくべきコストが 2 つあります。node 名を変えると保存済みの値が参照されなく
なりますが、名前の変更はファームウェアの変更であり、どのみち書き込み直しが必要です。
また、キーは node 名と同じ長さになり、上限は 48 byte です。収まらない名前は
切り詰められるのではなく、ビルドがはっきり失敗します。

永続化も registry が担います。driver 自身は何も保存しないため、再起動後に食い違う
2 つの所有者が生まれません。この option を無効にすると(既定であり、upstream ZMK
でのビルドもこの状態です)、この部分は何もコンパイルされず、devicetree の値で
固定されます。

保存済みの値は `zmk_custom_settings_initialized` でハードウェアへ反映されます。
これは、起動時の `settings_load()` を締めくくる settings subtree の commit で
1 回だけ発生する event です。これが唯一の正しい合図である理由は 2 つあり、
どちらも間違えやすいものです。

- **`SYS_INIT` では早すぎます。** `settings_load()` より前に実行されるため、
  devicetree の既定値を読みます。値は保存され、client にも正しく表示されるのに、
  そのセッションで誰かが書き直すまでハードウェアには一切反映されません。
- **`zmk_custom_setting_changed` は読み込み時には発生しません。** 保存領域から
  読み込まれた値はこの event なしで registry に適用されるため、この event だけを
  待つ listener も保存済みの値を受け取れません。

そのため各設定ファイルは両方の event を購読し、すべての instance を読み直します。
これで起動時とその後のすべての編集に対応します。`CONFIG_SETTINGS` のない
ビルドではこの event は発生しませんが、それで正しい動作です。何も保存されておらず、
driver はすでに devicetree の値で動いているからです。

この option には `zmk-feature-custom-settings` が必要であり、したがって custom
Studio RPC protocol を持つ ZMK が必要です。processor は C から直接操作することも
できます。`include/zmk-input-processors/runtime_scaler.h` を参照してください。

## プロジェクト構成

```
.
├── CMakeLists.txt                    # Root CMake configuration
├── Kconfig                           # Root Kconfig
├── drivers/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   └── input/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       ├── input_processor_runtime_code_mapper.c
│       ├── input_processor_runtime_code_mapper_custom_settings.c
│       ├── input_processor_runtime_scaler.c
│       ├── input_processor_runtime_scaler_custom_settings.c
│       ├── input_processor_runtime_temp_layer.c
│       ├── input_processor_runtime_temp_layer_custom_settings.c
│       ├── input_processor_runtime_transform.c
│       ├── input_processor_runtime_transform_custom_settings.c
│       └── input_processors_custom_settings.c   # the module's namespace
├── include/
│   └── zmk-input-processors/       # runtime APIs and the settings namespace
├── dts/
│   ├── bindings/
│   │   ├── zmk,input-processor-runtime-scaler.yaml
│   │   ├── zmk,input-processor-runtime-transform.yaml
│   │   ├── zmk,input-processor-runtime-code-mapper.yaml
│   │   └── zmk,input-processor-runtime-temp-layer.yaml
│   └── zmk-input-processors/input_processor_runtime.dtsi
├── tests/                             # host and ZMK integration suites
├── zephyr/
│   └── module.yml                    # Zephyr module registration
└── .github/
    └── CONTRIBUTING.md               # contributor guide
```

## 開発

### テスト

すべてのテストは、ファームウェアのビルドと同系統の ZMK build image を使い、
ソースツリーを読み取り専用で mount した Docker の中で実行します。

```bash
bash ./tests/run-docker.sh
bash ./tests/run-integration-docker.sh
```

`run-docker.sh` は `tests/unit/` の契約テストを実行します。driver が呼び出す、
依存のないロジックのヘッダーごとに 1 つのプログラムがあります。それぞれを
最適化ビルド、AddressSanitizer と UndefinedBehaviorSanitizer 付きのビルド、
ファームウェアと同じ 32-bit プログラムの 3 通りでビルドし、ヘッダーは Zephyr なしで
`-Wconversion` 付きの単独コンパイルも通す必要があります。scaler は、反転を発見した
実機セッションで記録した値、参照モデル、100 万件のランダムケース、移動量が失われも
増えもしないという不変条件で検査します。transform、code map、temp-layer policy は
入力の全範囲で検査します。

`run-integration-docker.sh` は、`zmk-feature-custom-settings` を含む DYA ZMK fork と、
それを含まない upstream ZMK を取得し(キーボード設定や他の input processor module は
含みません)、4 つの suite を実行します。

| Suite | 確認する内容 |
| :--- | :--- |
| `firmware` | すべての processor を 1 つずつ持つ shield が実機 board 向けにビルドでき、すべての設定キーが image に含まれること。1 つの devicetree を共有する split キーボードの両半分がビルドでき、central 側は設定付き、peripheral 側は設定と central 専用の temp layer なしでビルドされること。 |
| `guards` | 文書化した各上限を超える devicetree がそれぞれ専用のメッセージでビルド時に拒否され、上限ちょうどの devicetree はビルドできること。 |
| `runtime` | native_sim 上で snapshot と比較し、各 driver の API、event の絞り込み、ZMK 自身の keymap に対する temp-layer のタイミング、公開する各設定の型・既定値・範囲、公開先の Studio subsystem、driver への書き込みの反映、再起動後の保存値の再適用、起動時の重複キー報告、手計算した HID report を生成する listener chain、`input_processor_runtime.dtsi` の既定 node が上記の既定値で始まることを確認します。 |
| `upstream` | custom settings を必要としない runtime ケースを、`zmk-feature-custom-settings` のない upstream ZMK でビルド・実行し、冒頭の説明どおり動作することを確認します。 |

suite 名を指定すると一部だけ実行できます。Docker volume を使うと ZMK の workspace が
実行間で保持され、取得が更新だけになります。

```bash
ZMK_TEST_WORKSPACE_VOLUME=zmk-input-processors-tests \
  bash ./tests/run-integration-docker.sh runtime
```

runtime のケースは `tests/integration/runtime/` 以下のディレクトリで、ZMK 自身の
テストと同じく `native_sim.keymap`、`native_sim.conf`、sed script の
`events.patterns`、出力すべき内容の `keycode_events.snapshot` を持ちます。失敗した
ケースは差分を表示します。`tests/integration/guards/` 以下の guard ケースは、
出すべきコンパイルエラーを `expected-errors.txt` に過不足なく列挙します。ZMK は
`-Wfatal-errors` でコンパイルするため、guard ケースで壊す node はソースファイル
1 つにつき最大 1 つです。

### 新しい input processor の追加

module の構成、正しさの規則、必要なテスト、ローカル checkout での作業手順は
[contributor guide](.github/CONTRIBUTING.md) を参照してください。scaler の
[driver](drivers/input/input_processor_runtime_scaler.c) と隣の
`*_custom_settings.c` adapter が、実行時パラメーターとその任意の Studio 公開を
備えた最小の完全な例です。

## License

[MIT](LICENSE)。著作権表記は各ファイルのヘッダーを参照してください。

## Contributing

コントリビューションを歓迎します。[.github/CONTRIBUTING.md](.github/CONTRIBUTING.md)
に記載したコーディングパターンと規約に従ってください。
