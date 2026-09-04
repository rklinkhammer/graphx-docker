import React, { Component } from 'react'
import ReactDOM from 'react-dom/client'
import '@xyflow/react/dist/style.css'
import './styles.css'
import App from './App'

class ConsoleErrorBoundary extends Component {
  constructor(props) {
    super(props)
    this.state = { error: null }
  }

  static getDerivedStateFromError(error) {
    return { error }
  }

  render() {
    if (!this.state.error) return this.props.children
    return <main className="console-error"><section>
      <h1>GraphX console could not render this view</h1>
      <p>{this.state.error.message}</p>
      <button onClick={() => window.location.reload()}>Reload console</button>
    </section></main>
  }
}

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode><ConsoleErrorBoundary><App/></ConsoleErrorBoundary></React.StrictMode>,
)
