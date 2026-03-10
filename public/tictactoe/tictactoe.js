const { createApp, ref, computed } = Vue;

createApp({
  setup() {
    const board = ref(Array(9).fill(''));
    const currentPlayer = ref('X');
    const gameOver = ref(false);
    const winner = ref(null);
    

    const statusMessage = computed(() => {
      if (winner.value) {
        return `Player ${winner.value} wins!`;
      } else if (gameOver.value) {
        return "Game over - It's a draw!";
      } else {
        return "You are Xs";
      }
    });

    const computerMove = () => {
      if (gameOver.value) return;
      
      currentPlayer.value = 'O';      

      const winMove = findWinningMove('O');
      if (winMove !== -1) {
        board.value[winMove] = 'O';
      }

      else {
        const blockMove = findWinningMove('X');
        if (blockMove !== -1) {
          board.value[blockMove] = 'O';
        }

        else if (board.value[4] === '') {
          board.value[4] = 'O';
        }

        else {
          const corners = [0, 2, 6, 8];
          const availableCorners = corners.filter(i => board.value[i] === '');
          
          if (availableCorners.length > 0) {
            const randomCorner = availableCorners[Math.floor(Math.random() * availableCorners.length)];
            board.value[randomCorner] = 'O';
          }

          else {
            const edges = [1, 3, 5, 7];
            const availableEdges = edges.filter(i => board.value[i] === '');
            
            if (availableEdges.length > 0) {
              const randomEdge = availableEdges[Math.floor(Math.random() * availableEdges.length)];
              board.value[randomEdge] = 'O';
            }
          }
        }
      }

      const potentialWinner = checkWinner();
      if (potentialWinner) {
        winner.value = potentialWinner;
        gameOver.value = true;
        return;
      }
    

      if (isBoardFull()) {
        gameOver.value = true;
      }
    };
    
    const findWinningMove = (player) => {
      const winPatterns = [
        [0, 1, 2], [3, 4, 5], [6, 7, 8],
        [0, 3, 6], [1, 4, 7], [2, 5, 8],
        [0, 4, 8], [2, 4, 6]
      ];
      
      for (const pattern of winPatterns) {
        const [a, b, c] = pattern;

        if (board.value[a] === player && 
            board.value[b] === player && 
            board.value[c] === '') {
          return c;
        }
        if (board.value[a] === player && 
            board.value[c] === player && 
            board.value[b] === '') {
          return b;
        }
        if (board.value[b] === player && 
            board.value[c] === player && 
            board.value[a] === '') {
          return a;
        }
      }
      
      return -1;
    };
    

    const checkWinner = () => {
      const winPatterns = [
        [0, 1, 2], [3, 4, 5], [6, 7, 8],
        [0, 3, 6], [1, 4, 7], [2, 5, 8],
        [0, 4, 8], [2, 4, 6] 
      ];
      
      for (const pattern of winPatterns) {
        const [a, b, c] = pattern;
        if (board.value[a] && board.value[a] === board.value[b] && board.value[a] === board.value[c]) {
          return board.value[a];
        }
      }
      
      return null;
    };
    
    const isBoardFull = () => {
      return board.value.every(cell => cell !== '');
    };
    
    const makeMove = (index) => {
      if (gameOver.value || board.value[index] !== '') {
        return;
      }
      
      board.value[index] = currentPlayer.value;
      
      const potentialWinner = checkWinner();
      if (potentialWinner) {
        winner.value = potentialWinner;
        gameOver.value = true;
        return;
      }

      if (isBoardFull()) {
        gameOver.value = true;
        return;
      }
      
      computerMove();
      currentPlayer.value = 'X';
    };
   
const back_to_home = async () => {
    try {
      const response = await fetch("/");
      if (!response.ok) {
        throw new Error(`Response status: ${response.status}`);
      }

      const json = await response.json();
      console.log(json);
    } catch (err) {
      console.error("Fetch error:", err);
    }
};

    const resetGame = () => {
      board.value = Array(9).fill('');
      currentPlayer.value = 'X';
      gameOver.value = false;
      winner.value = null;
    };
    
    return {
      board,
      currentPlayer,
      statusMessage,
      makeMove,
      resetGame,
      back_to_home
    };
  }
}).mount('#app');
