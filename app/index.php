<?php
// Trigger the JIT generic (func==NULL) observer-begin path: a MEGAMORPHIC call site
// to an observed user method. With jit_max_polymorphic_calls=2, 3+ receiver types make
// the call site megamorphic, so the tracing JIT cannot statically resolve the callee and
// emits the runtime "type==INTERNAL ? internal_ext : op_array_ext" handle selection.
declare(strict_types=1);

interface Section { public function setSection(string $n): int; }

final class A implements Section { private array $s = []; public function setSection(string $n): int { $this->s[$n] = ($this->s[$n] ?? 0) + 1; return count($this->s); } }
final class B implements Section { private array $s = []; public function setSection(string $n): int { $this->s[$n] = ($this->s[$n] ?? 0) + 2; return count($this->s); } }
final class C implements Section { private array $s = []; public function setSection(string $n): int { $this->s[$n] = ($this->s[$n] ?? 0) + 3; return count($this->s); } }
final class D implements Section { private array $s = []; public function setSection(string $n): int { $this->s[$n] = ($this->s[$n] ?? 0) + 4; return count($this->s); } }
final class E implements Section { private array $s = []; public function setSection(string $n): int { $this->s[$n] = ($this->s[$n] ?? 0) + 5; return count($this->s); } }

/** @var Section[] $objs */
$objs = [new A(), new B(), new C(), new D(), new E()];
$m = count($objs);

$n = (int) ($_GET['n'] ?? 3_000_000);
$total = 0;
for ($i = 0; $i < $n; $i++) {
    $o = $objs[$i % $m];          // receiver type varies -> megamorphic call site
    $total += $o->setSection('s' . ($i & 7));
}

header('Content-Type: text/plain');
echo "pid=" . getmypid() . " total=" . $total . " jit=" . (opcache_get_status()['jit']['enabled'] ? '1' : '0') . "\n";
