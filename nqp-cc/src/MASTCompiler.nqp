use MASTNodes;
use MASTOps;

class MAST::Compiler {
    method compile($node, $target_file) {
        if (pir::new__Ps('Env')<MVMCCDEBUG>) { say($node.dump); }
        pir::mvm_compiler_setup__vPPPPPPPPPPPP(
            MAST::CompUnit,
            MAST::Frame,
            MAST::Op,
            MAST::SVal,
            MAST::IVal,
            MAST::NVal,
            MAST::Label,
            MAST::Local,
            MAST::Lexical,
            MAST::Call,
            MAST::Annotated,
            MAST::HandlerScope);
        pir::mvm_compile__vPs($node, $target_file);
        if (pir::new__Ps('Env')<MVMCCDEBUG>) {
            pir::spawnw__Is("moarvm --dump $target_file > $target_file.mvmdump");
        }
    }
}
